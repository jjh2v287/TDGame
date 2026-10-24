#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicRHI.h"
#include "HAL/IConsoleManager.h"
#include "Math/Float16.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RHIGlobals.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuStepPasses.h"
#include "TDFluidGpuWaterPressureProbeReadbacks.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidGpuWaterModuleRhiTests, Log, All);

namespace TDFluidGpuWaterModuleRhiTestPrivate
{
	constexpr float SixtyHertzStepSeconds = 1.0f / 60.0f;
	constexpr double ModuleWaterExtentCm = 3600.0;
	constexpr uint64 ModuleWaterActorKey = 4101;
	constexpr float ModuleWaterSurfaceHeightCm = 100.0f;
	constexpr int32 ChannelFieldRowNumerator = 3;
	constexpr int32 ChannelFieldRowEndNumerator = 7;
	constexpr int32 ChannelFieldRowDenominator = 10;
	constexpr int32 PillarPatchHalfWidthCells = 48;
	constexpr int32 PillarPatchHalfHeightCells = 16;
	constexpr double PillarRadiusInCells = 0.3;
	constexpr double WaterHalfPressureDirichletScaleLevelOne = 4.0 / 3.0;

	using FTDWaterGpuFrameBuilder = TFunction<void(int32, FTDFluidGpuFrameInputs&)>;

	class FTDWaterGpuConsoleVariableScope
	{
	public:
		FTDWaterGpuConsoleVariableScope(const TCHAR* Name, int32 Value)
			: ConsoleVariable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (!ConsoleVariable)
			{
				return;
			}

			PreviousValue = ConsoleVariable->GetString();
			ConsoleVariable->Set(Value, ECVF_SetByCode);
			bIsApplied = ConsoleVariable->GetInt() == Value;
		}

		~FTDWaterGpuConsoleVariableScope()
		{
			if (!ConsoleVariable)
			{
				return;
			}

			ConsoleVariable->Set(*PreviousValue, ECVF_SetByCode);
		}

		FTDWaterGpuConsoleVariableScope(const FTDWaterGpuConsoleVariableScope&) = delete;
		FTDWaterGpuConsoleVariableScope& operator=(const FTDWaterGpuConsoleVariableScope&) = delete;

		bool IsValid() const
		{
			return ConsoleVariable != nullptr && bIsApplied;
		}

	private:
		IConsoleVariable* ConsoleVariable = nullptr;
		FString PreviousValue;
		bool bIsApplied = false;
	};

	enum class ETDWaterGpuStateTexture : uint8
	{
		Velocity,
		VelocityScratch,
		Density,
		Ripple,
		RippleScratch,
		Pressure,
		PressureScratch,
		GuideField,
		SurfaceHeightField,
		TerrainField,
		Count
	};

	constexpr int32 WaterGpuStateTextureCount = static_cast<int32>(ETDWaterGpuStateTexture::Count);

	struct FTDWaterGpuTextureSlice
	{
		int32 Resolution = 0;
		EPixelFormat Format = PF_Unknown;
		TArray<uint8> Bytes;
	};

	struct FTDWaterGpuStateSnapshot
	{
		int32 FrameCount = 0;
		FTDFluidGpuWaterParameters WaterParameters;
		FTDWaterGpuTextureSlice Textures[WaterGpuStateTextureCount];
	};

	struct FTDWaterGpuPressureProbeCapture
	{
		bool bIsValid = false;
		FTDWaterGpuTextureSlice Velocity;
		FTDWaterGpuTextureSlice CompositeSdf;
		FTDWaterGpuTextureSlice SurfaceHeightField;
		TArray<int32> RecordedCycleCounts;
		TArray<FTDWaterGpuTextureSlice> PressureFullByCycle;
		uint32 OpenBorderFlag = 0;
	};

	struct FTDWaterGpuRunRequest
	{
		int32 FrameCount = 1;
		TArray<int32> SnapshotFrameCounts;
		TArray<ETDWaterGpuStateTexture> SnapshotTextures;
		TArray<int32> ProbeCycleCounts;
	};

	struct FTDWaterGpuRunResult
	{
		bool bHasWaterBatch = false;
		bool bHasReadAllSnapshots = false;
		int64 WaterStateBytes = 0;
		int32 WaterSliceCapacity = 0;
		FTDFluidGpuBatchKey WaterKey;
		FTDFluidGpuActorParameters WaterActorParameters;
		FTDFluidGpuWaterParameters WaterParameters;
		TArray<FTDWaterGpuStateSnapshot> Snapshots;
		FTDWaterGpuPressureProbeCapture Probe;
	};

	struct FTDWaterGpuRunState
	{
		TArray<FTDFluidGpuBatch> Batches;
		TArray<TArray<TUniquePtr<FRHIGPUTextureReadback>>> SnapshotReadbacks;
		FTDFluidGpuWaterPressureProbeReadbacks ProbeReadbacks;
		EPixelFormat ProbeVelocityFormat = PF_Unknown;
		int32 ProbeResolution = 0;
		int32 ProbeFieldResolution = 0;
		int32 ProbeEntryCount = 0;
		FTDWaterGpuRunResult Result;
	};

	bool CanRunWaterModuleRhiTest(FAutomationTestBase& Test, const TCHAR* TestLabel)
	{
		const bool bHasRenderingRhi = !GUsingNullRHI && FApp::CanEverRender();
		UE_LOG(LogTDFluidGpuWaterModuleRhiTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), TestLabel, bHasRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (bHasRenderingRhi)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), TestLabel));
		return false;
	}

	int32 WrapFieldCoordinate(int32 Coordinate, int32 FieldResolution)
	{
		return ((Coordinate % FieldResolution) + FieldResolution) % FieldResolution;
	}

	bool IsInsideGrid(const FIntPoint& Cell, int32 Resolution)
	{
		return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Resolution && Cell.Y < Resolution;
	}

	int32 GetSliceBytesPerTexel(const FTDWaterGpuTextureSlice& Slice)
	{
		return GPixelFormats[Slice.Format].BlockBytes;
	}

	bool IsSliceReadable(const FTDWaterGpuTextureSlice& Slice)
	{
		return Slice.Resolution > 0 && Slice.Bytes.Num() == Slice.Resolution * Slice.Resolution * GetSliceBytesPerTexel(Slice);
	}

	int64 ComputeSliceByteOffset(const FTDWaterGpuTextureSlice& Slice, int32 CellX, int32 CellY)
	{
		return (static_cast<int64>(CellY) * Slice.Resolution + CellX) * GetSliceBytesPerTexel(Slice);
	}

	bool IsHalfFloatTextureFormat(EPixelFormat Format)
	{
		return Format == PF_R16F || Format == PF_G16R16F || Format == PF_FloatRGBA;
	}

	float ReadSliceChannel(const FTDWaterGpuTextureSlice& Slice, int32 CellX, int32 CellY, int32 Channel)
	{
		const uint8* Texel = Slice.Bytes.GetData() + ComputeSliceByteOffset(Slice, CellX, CellY);
		if (IsHalfFloatTextureFormat(Slice.Format))
		{
			FFloat16 HalfValue;
			FMemory::Memcpy(&HalfValue, Texel + Channel * sizeof(FFloat16), sizeof(FFloat16));
			return HalfValue.GetFloat();
		}

		float FloatValue = 0.0f;
		FMemory::Memcpy(&FloatValue, Texel + Channel * sizeof(float), sizeof(float));
		return FloatValue;
	}

	bool AreSliceTexelsBitIdentical(const FTDWaterGpuTextureSlice& Left, const FIntPoint& LeftCell, const FTDWaterGpuTextureSlice& Right, const FIntPoint& RightCell)
	{
		const int32 BytesPerTexel = GetSliceBytesPerTexel(Left);
		if (BytesPerTexel != GetSliceBytesPerTexel(Right))
		{
			return false;
		}

		return FMemory::Memcmp(Left.Bytes.GetData() + ComputeSliceByteOffset(Left, LeftCell.X, LeftCell.Y), Right.Bytes.GetData() + ComputeSliceByteOffset(Right, RightCell.X, RightCell.Y), BytesPerTexel) == 0;
	}

	bool ReadTextureSlice(FRHIGPUTextureReadback* Readback, FTDWaterGpuTextureSlice& InOutSlice)
	{
		if (!Readback || !Readback->IsReady() || InOutSlice.Resolution <= 0 || InOutSlice.Format == PF_Unknown)
		{
			return false;
		}

		const int32 Resolution = InOutSlice.Resolution;
		const int32 BytesPerTexel = GetSliceBytesPerTexel(InOutSlice);
		int32 RowPitchInPixels = 0;
		int32 BufferHeight = 0;
		const uint8* Texels = static_cast<const uint8*>(Readback->Lock(RowPitchInPixels, &BufferHeight));
		if (!Texels || RowPitchInPixels < Resolution || BufferHeight < Resolution)
		{
			Readback->Unlock();
			return false;
		}

		InOutSlice.Bytes.SetNumUninitialized(Resolution * Resolution * BytesPerTexel);
		for (int32 RowIndex = 0; RowIndex < Resolution; ++RowIndex)
		{
			FMemory::Memcpy(InOutSlice.Bytes.GetData() + static_cast<int64>(RowIndex) * Resolution * BytesPerTexel, Texels + static_cast<int64>(RowIndex) * RowPitchInPixels * BytesPerTexel, Resolution * BytesPerTexel);
		}
		Readback->Unlock();
		return true;
	}

	FTDWaterGpuTextureSlice MakeEmptySlice(int32 Resolution, EPixelFormat Format)
	{
		FTDWaterGpuTextureSlice Slice;
		Slice.Resolution = Resolution;
		Slice.Format = Format;
		return Slice;
	}

	FTDFluidGpuBatch* FindSteppedWaterBatch(TArray<FTDFluidGpuBatch>& Batches)
	{
		return Batches.FindByPredicate([](const FTDFluidGpuBatch& Batch)
		{
			return Batch.GetKey().bUsesWaterLayer && !Batch.GetFrame().Parameters.IsEmpty() && !Batch.GetFrame().WaterParameters.IsEmpty();
		});
	}

	const TRefCountPtr<IPooledRenderTarget>& SelectStateTexture(const FTDFluidGpuBatchStateTextures& State, ETDWaterGpuStateTexture TextureKind)
	{
		switch (TextureKind)
		{
		case ETDWaterGpuStateTexture::Velocity:
			return State.Velocity[0];
		case ETDWaterGpuStateTexture::VelocityScratch:
			return State.Velocity[1];
		case ETDWaterGpuStateTexture::Density:
			return State.Density[0];
		case ETDWaterGpuStateTexture::Ripple:
			return State.Ripple[0];
		case ETDWaterGpuStateTexture::RippleScratch:
			return State.Ripple[1];
		case ETDWaterGpuStateTexture::Pressure:
			return State.PressureSolution[0];
		case ETDWaterGpuStateTexture::PressureScratch:
			return State.PressureSolution[1];
		case ETDWaterGpuStateTexture::GuideField:
			return State.WaterGuideField;
		case ETDWaterGpuStateTexture::SurfaceHeightField:
			return State.WaterSurfaceHeightField;
		default:
			return State.WaterTerrainField;
		}
	}

	void EnqueueWaterStateSnapshot(FRHICommandListImmediate& RHICmdList, int32 FrameCount, const TArray<ETDWaterGpuStateTexture>& TextureKinds, FTDWaterGpuRunState& InOutRunState)
	{
		FTDFluidGpuBatch* WaterBatch = FindSteppedWaterBatch(InOutRunState.Batches);
		if (!WaterBatch)
		{
			return;
		}

		const FTDFluidGpuBatchStateTextures& State = WaterBatch->StateTextures;
		const uint32 SliceIndex = static_cast<uint32>(WaterBatch->GetFrame().Parameters[0].SliceIndex);
		FTDWaterGpuStateSnapshot& Snapshot = InOutRunState.Result.Snapshots.AddDefaulted_GetRef();
		Snapshot.FrameCount = FrameCount;
		Snapshot.WaterParameters = WaterBatch->GetFrame().WaterParameters[0];
		TArray<TUniquePtr<FRHIGPUTextureReadback>>& Readbacks = InOutRunState.SnapshotReadbacks.AddDefaulted_GetRef();
		Readbacks.SetNum(WaterGpuStateTextureCount);

		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuWaterModuleRhiSnapshot"));
		for (const ETDWaterGpuStateTexture TextureKind : TextureKinds)
		{
			const TRefCountPtr<IPooledRenderTarget>& Source = SelectStateTexture(State, TextureKind);
			if (!Source.IsValid())
			{
				continue;
			}

			const int32 TextureIndex = static_cast<int32>(TextureKind);
			Snapshot.Textures[TextureIndex] = MakeEmptySlice(Source->GetDesc().Extent.X, Source->GetDesc().Format);
			Readbacks[TextureIndex] = MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.WaterModuleRhiSnapshotReadback"));
			AddEnqueueCopyPass(GraphBuilder, Readbacks[TextureIndex].Get(), GraphBuilder.RegisterExternalTexture(Source, TEXT("TDFluidGpu.WaterModuleRhiSnapshotSource")), FResolveRect(), SliceIndex);
		}
		GraphBuilder.Execute();
	}

	void EnqueueWaterPressureProbe(FRHICommandListImmediate& RHICmdList, const TArray<int32>& CycleCounts, FTDWaterGpuRunState& InOutRunState)
	{
		FTDFluidGpuBatch* WaterBatch = FindSteppedWaterBatch(InOutRunState.Batches);
		if (!WaterBatch || CycleCounts.IsEmpty() || !WaterBatch->StateTextures.Velocity[1].IsValid() || !WaterBatch->StateTextures.WaterSurfaceHeightField.IsValid())
		{
			return;
		}

		InOutRunState.ProbeVelocityFormat = WaterBatch->StateTextures.Velocity[1]->GetDesc().Format;
		InOutRunState.ProbeResolution = WaterBatch->GetKey().ResolutionTier;
		InOutRunState.ProbeFieldResolution = WaterBatch->StateTextures.WaterSurfaceHeightField->GetDesc().Extent.X;
		InOutRunState.ProbeEntryCount = WaterBatch->GetFrame().Entries.Num();
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuWaterModuleRhiPressureProbe"));
		AddTDFluidGpuWaterPressureProbePasses(GraphBuilder, *WaterBatch, 0, CycleCounts, InOutRunState.ProbeReadbacks);
		GraphBuilder.Execute();
	}

	bool ReadWaterStateSnapshots(FTDWaterGpuRunState& InOutRunState)
	{
		TArray<FTDWaterGpuStateSnapshot>& Snapshots = InOutRunState.Result.Snapshots;
		bool bHasReadAll = Snapshots.Num() == InOutRunState.SnapshotReadbacks.Num();
		for (int32 SnapshotIndex = 0; bHasReadAll && SnapshotIndex < Snapshots.Num(); ++SnapshotIndex)
		{
			for (int32 TextureIndex = 0; TextureIndex < WaterGpuStateTextureCount; ++TextureIndex)
			{
				FRHIGPUTextureReadback* Readback = InOutRunState.SnapshotReadbacks[SnapshotIndex][TextureIndex].Get();
				if (Readback)
				{
					bHasReadAll &= ReadTextureSlice(Readback, Snapshots[SnapshotIndex].Textures[TextureIndex]);
				}
			}
		}
		return bHasReadAll;
	}

	void ReadWaterPressureProbe(FTDWaterGpuRunState& InOutRunState)
	{
		FTDFluidGpuWaterPressureProbeReadbacks& Readbacks = InOutRunState.ProbeReadbacks;
		FTDWaterGpuPressureProbeCapture& Capture = InOutRunState.Result.Probe;
		if (!Readbacks.Velocity.IsValid() || !Readbacks.CompositeSdf.IsValid() || !Readbacks.SurfaceHeightField.IsValid() || !Readbacks.OpenBorderFlags.IsValid())
		{
			return;
		}

		Capture.Velocity = MakeEmptySlice(InOutRunState.ProbeResolution, InOutRunState.ProbeVelocityFormat);
		Capture.CompositeSdf = MakeEmptySlice(InOutRunState.ProbeResolution, PF_R16F);
		Capture.SurfaceHeightField = MakeEmptySlice(InOutRunState.ProbeFieldResolution, PF_R32_FLOAT);
		bool bHasReadAll = ReadTextureSlice(Readbacks.Velocity.Get(), Capture.Velocity);
		bHasReadAll &= ReadTextureSlice(Readbacks.CompositeSdf.Get(), Capture.CompositeSdf);
		bHasReadAll &= ReadTextureSlice(Readbacks.SurfaceHeightField.Get(), Capture.SurfaceHeightField);
		for (const TUniquePtr<FRHIGPUTextureReadback>& PressureReadback : Readbacks.PressureFullByCycle)
		{
			FTDWaterGpuTextureSlice& PressureSlice = Capture.PressureFullByCycle.Add_GetRef(MakeEmptySlice(InOutRunState.ProbeResolution, PF_R32_FLOAT));
			bHasReadAll &= ReadTextureSlice(PressureReadback.Get(), PressureSlice);
		}

		const uint32 FlagBytes = static_cast<uint32>(FMath::Max(InOutRunState.ProbeEntryCount, 1) * sizeof(uint32));
		const uint32* OpenBorderFlags = Readbacks.OpenBorderFlags->IsReady() ? static_cast<const uint32*>(Readbacks.OpenBorderFlags->Lock(FlagBytes)) : nullptr;
		if (OpenBorderFlags)
		{
			Capture.OpenBorderFlag = OpenBorderFlags[0];
			Readbacks.OpenBorderFlags->Unlock();
		}

		Capture.RecordedCycleCounts = Readbacks.RecordedCycleCounts;
		Capture.bIsValid = bHasReadAll && OpenBorderFlags != nullptr;
	}

	void RecordWaterBatchSummary(FTDWaterGpuRunState& InOutRunState)
	{
		FTDFluidGpuBatch* WaterBatch = FindSteppedWaterBatch(InOutRunState.Batches);
		if (!WaterBatch)
		{
			return;
		}

		FTDWaterGpuRunResult& Result = InOutRunState.Result;
		Result.bHasWaterBatch = true;
		Result.WaterStateBytes = ComputeTDFluidGpuBatchStateBytes(*WaterBatch);
		Result.WaterSliceCapacity = WaterBatch->StateTextures.SliceCapacity;
		Result.WaterKey = WaterBatch->GetKey();
		Result.WaterActorParameters = WaterBatch->GetFrame().Parameters[0];
		Result.WaterParameters = WaterBatch->GetFrame().WaterParameters[0];
	}

	FTDWaterGpuRunResult RunWaterGpuFrames(const FTDWaterGpuRunRequest& Request, const FTDWaterGpuFrameBuilder& BuildFrame)
	{
		TSharedRef<FTDWaterGpuRunState, ESPMode::ThreadSafe> RunState = MakeShared<FTDWaterGpuRunState, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuWaterModuleRhiFrames)(
			[RunState, Request, BuildFrame](FRHICommandListImmediate& RHICmdList)
			{
				for (int32 FrameIndex = 0; FrameIndex < Request.FrameCount; ++FrameIndex)
				{
					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
					BuildFrame(FrameIndex, FrameInputs);
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuWaterModuleRhiFrame"));
						AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, RunState->Batches);
						GraphBuilder.Execute();
					}
					if (Request.SnapshotFrameCounts.Contains(FrameIndex + 1))
					{
						EnqueueWaterStateSnapshot(RHICmdList, FrameIndex + 1, Request.SnapshotTextures, *RunState);
					}
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}

				EnqueueWaterPressureProbe(RHICmdList, Request.ProbeCycleCounts, *RunState);
				RHICmdList.SubmitAndBlockUntilGPUIdle();
				RunState->Result.bHasReadAllSnapshots = ReadWaterStateSnapshots(*RunState);
				ReadWaterPressureProbe(*RunState);
				RecordWaterBatchSummary(*RunState);
				RunState->SnapshotReadbacks.Empty();
				RunState->ProbeReadbacks = FTDFluidGpuWaterPressureProbeReadbacks();
				RunState->Batches.Empty();
			});
		FlushRenderingCommands();
		return RunState->Result;
	}

	const FTDWaterGpuTextureSlice& GetSnapshotTexture(const FTDWaterGpuStateSnapshot& Snapshot, ETDWaterGpuStateTexture TextureKind)
	{
		return Snapshot.Textures[static_cast<int32>(TextureKind)];
	}

	FTDFluidGpuSimulationSettings MakeModuleWaterSettings(int32 Resolution, bool bOpensWaterBorders)
	{
		FTDFluidGpuSimulationSettings Settings;
		Settings.SimMode = ETDFluidGpuSimMode::FluidSim;
		Settings.Resolution = Resolution;
		Settings.ExtentCm = FVector2D(ModuleWaterExtentCm, ModuleWaterExtentCm);
		Settings.PressureIterations = 12;
		Settings.VorticityStrength = 0.0f;
		Settings.TerrainSlopeGravity = 0.0f;
		Settings.bUsesWaterLayer = true;
		Settings.bOpenWaterBorders = bOpensWaterBorders;
		Settings.bTreatDryCellsAsSolid = true;
		Settings.RippleWaveSpeedCmPerSec = 0.0f;
		FTDFluidGpuBatch::ApplyWaterConsoleOverrides(Settings);
		return Settings;
	}

	int32 GetWaterTier(const FTDFluidGpuSimulationSettings& Settings)
	{
		return FTDFluidGpuBatch::ResolveWaterResolutionTier(Settings.Resolution);
	}

	int32 GetWaterFieldDivisor(const FTDFluidGpuSimulationSettings& Settings)
	{
		return FTDFluidGpuBatch::ComputeWaterFieldDivisor(GetWaterTier(Settings));
	}

	int32 GetWaterFieldResolution(const FTDFluidGpuSimulationSettings& Settings)
	{
		return GetWaterTier(Settings) / GetWaterFieldDivisor(Settings);
	}

	FVector2D ComputeWaterCellSize(const FTDFluidGpuSimulationSettings& Settings)
	{
		return Settings.ExtentCm / static_cast<double>(GetWaterTier(Settings));
	}

	FVector2D ComputeCellCenterPlaneCm(const FTDFluidGpuSimulationSettings& Settings, const FIntPoint& Cell)
	{
		const FVector2D CellSize = ComputeWaterCellSize(Settings);
		return FVector2D((Cell.X + 0.5) * CellSize.X, (Cell.Y + 0.5) * CellSize.Y) - 0.5 * Settings.ExtentCm;
	}

	struct FTDWaterGpuChannelScene
	{
		FTDFluidGpuSimulationSettings Settings;
		float FlowSpeedCmPerSec = 150.0f;
		float TerrainElevationCm = 0.0f;
		float RockRadiusCm = 0.0f;
		bool bAddsPillarPatch = false;
	};

	bool IsChannelFieldRow(int32 FieldRow, int32 FieldResolution)
	{
		return FieldRow >= FieldResolution * ChannelFieldRowNumerator / ChannelFieldRowDenominator && FieldRow < FieldResolution * ChannelFieldRowEndNumerator / ChannelFieldRowDenominator;
	}

	void AddChannelFieldUpdates(const FTDWaterGpuChannelScene& Scene, FTDFluidGpuActorInput& InOutActor)
	{
		const int32 FieldResolution = GetWaterFieldResolution(Scene.Settings);
		InOutActor.WaterGuideUpdates.Reserve(FieldResolution * FieldResolution);
		InOutActor.WaterTerrainUpdates.Reserve(FieldResolution * FieldResolution);
		for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
		{
			const bool bIsChannelRow = IsChannelFieldRow(FieldY, FieldResolution);
			for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
			{
				FTDFluidGpuWaterGuideCellUpdate& GuideUpdate = InOutActor.WaterGuideUpdates.AddDefaulted_GetRef();
				GuideUpdate.FieldCell = FIntPoint(FieldX, FieldY);
				GuideUpdate.SurfaceHeightCm = bIsChannelRow ? ModuleWaterSurfaceHeightCm : TDFluidGpuSolverConstants::WaterNoSurfaceHeightCm;
				GuideUpdate.TargetVelocityCmPerSec = bIsChannelRow ? FVector2f(Scene.FlowSpeedCmPerSec, 0.0f) : FVector2f::ZeroVector;
				GuideUpdate.GuideWeight = bIsChannelRow ? 1.0f : 0.0f;
				GuideUpdate.GuideDescent = 0.0f;

				FTDFluidGpuTerrainCellUpdate& TerrainUpdate = InOutActor.WaterTerrainUpdates.AddDefaulted_GetRef();
				TerrainUpdate.Cell = FIntPoint(FieldX, FieldY);
				TerrainUpdate.ElevationCm = Scene.TerrainElevationCm;
			}
		}
	}

	FIntPoint GetPillarPatchMinimumCell(int32 Resolution)
	{
		return FIntPoint(Resolution / 2 - PillarPatchHalfWidthCells, Resolution / 2 - PillarPatchHalfHeightCells);
	}

	FIntPoint GetPillarPatchEndCell(int32 Resolution)
	{
		return FIntPoint(Resolution / 2 + PillarPatchHalfWidthCells, Resolution / 2 + PillarPatchHalfHeightCells);
	}

	bool IsGroupAPillarCell(const FIntPoint& Cell)
	{
		return Cell.X % 4 == 0 && Cell.Y % 4 == 0;
	}

	bool IsGroupCPillarCell(const FIntPoint& Cell)
	{
		return Cell.X % 4 == 3 && Cell.Y % 4 == 1;
	}

	void AddPillarPatch(const FTDWaterGpuChannelScene& Scene, FTDFluidGpuActorInput& InOutActor)
	{
		const int32 Resolution = GetWaterTier(Scene.Settings);
		const FIntPoint MinimumCell = GetPillarPatchMinimumCell(Resolution);
		const FIntPoint EndCell = GetPillarPatchEndCell(Resolution);
		const double PillarRadiusCm = PillarRadiusInCells * ComputeWaterCellSize(Scene.Settings).X;
		for (int32 CellY = MinimumCell.Y; CellY < EndCell.Y; ++CellY)
		{
			for (int32 CellX = MinimumCell.X; CellX < EndCell.X; ++CellX)
			{
				const FIntPoint Cell(CellX, CellY);
				if (!IsGroupAPillarCell(Cell) && !IsGroupCPillarCell(Cell))
				{
					continue;
				}

				FTDFluidGpuObstacleInput& Pillar = InOutActor.Obstacles.AddDefaulted_GetRef();
				Pillar.CenterPlaneCm = ComputeCellCenterPlaneCm(Scene.Settings, Cell);
				Pillar.RadiusCm = static_cast<float>(PillarRadiusCm);
			}
		}
	}

	FTDFluidGpuActorInput MakeChannelActor(const FTDWaterGpuChannelScene& Scene, int32 FrameIndex)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = ModuleWaterActorKey;
		Actor.Settings = Scene.Settings;
		Actor.DeltaTimeSeconds = SixtyHertzStepSeconds;
		Actor.SimulationTimeSeconds = FrameIndex * static_cast<double>(SixtyHertzStepSeconds);
		Actor.bHasObstacleListChanged = FrameIndex == 0;
		if (Scene.RockRadiusCm > 0.0f)
		{
			FTDFluidGpuObstacleInput& Rock = Actor.Obstacles.AddDefaulted_GetRef();
			Rock.CenterPlaneCm = FVector2D::ZeroVector;
			Rock.RadiusCm = Scene.RockRadiusCm;
		}
		if (Scene.bAddsPillarPatch)
		{
			AddPillarPatch(Scene, Actor);
		}
		if (FrameIndex == 0)
		{
			AddChannelFieldUpdates(Scene, Actor);
		}
		return Actor;
	}

	int64 ComputeExpectedWaterStateBytes(int32 Resolution, int32 FieldResolution, int32 PressureResolution, bool bUsesFloat32Velocity, bool bUsesFloat32Ripple)
	{
		const int64 CellCount = static_cast<int64>(Resolution) * Resolution;
		const int64 VelocityBytes = bUsesFloat32Velocity ? 8 : 4;
		const int64 RippleBytes = bUsesFloat32Ripple ? 8 : 4;
		const int64 PressureCellCount = static_cast<int64>(PressureResolution) * PressureResolution;
		const int64 FieldCellCount = static_cast<int64>(FieldResolution) * FieldResolution;
		return CellCount * (2 * VelocityBytes + 16 + 2 * RippleBytes + 2) + PressureCellCount * 8 + FieldCellCount * 24;
	}

	struct FTDWaterGpuShearScene
	{
		FTDFluidGpuSimulationSettings Settings;
		FIntPoint ShiftCells = FIntPoint::ZeroValue;
	};

	FTDFluidGpuSimulationSettings MakeShearSettings(int32 Resolution)
	{
		FTDFluidGpuSimulationSettings Settings = MakeModuleWaterSettings(Resolution, true);
		Settings.VelocityDamping = 0.0f;
		Settings.DensityDissipation = 0.0f;
		Settings.BorderFadeRatio = 0.0f;
		Settings.AmbientDensity = 0.0f;
		Settings.AmbientRefillPerSec = 0.0f;
		Settings.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Settings.WhitewaterFromVorticity = 0.0f;
		Settings.RapidsWhitewaterPerSec = 0.0f;
		Settings.RippleWaveSpeedCmPerSec = 0.0f;
		return Settings;
	}

	float ComputeShearFlowSpeed(int32 WorldFieldRow, int32 FieldResolution)
	{
		const int32 WrappedRow = WrapFieldCoordinate(WorldFieldRow, FieldResolution);
		if (WrappedRow >= FieldResolution / 4 && WrappedRow < FieldResolution / 2)
		{
			return 0.0f;
		}

		return 40.0f + 15.0f * static_cast<float>(WrappedRow % 5);
	}

	void AddShearGuideUpdate(int32 FieldResolution, const FIntPoint& FieldCell, const FIntPoint& FieldWindowOffset, FTDFluidGpuActorInput& InOutActor)
	{
		FTDFluidGpuWaterGuideCellUpdate& GuideUpdate = InOutActor.WaterGuideUpdates.AddDefaulted_GetRef();
		GuideUpdate.FieldCell = FieldCell;
		GuideUpdate.SurfaceHeightCm = ModuleWaterSurfaceHeightCm;
		GuideUpdate.TargetVelocityCmPerSec = FVector2f(ComputeShearFlowSpeed(FieldCell.Y + FieldWindowOffset.Y, FieldResolution), 0.0f);
		GuideUpdate.GuideWeight = 1.0f;
		GuideUpdate.GuideDescent = 0.0f;
	}

	FTDFluidGpuStampInput MakeModuleWaterStamp(const FVector2D& CenterPlaneCm, float RadiusCm, float DensityStrength, float RippleImpulseCm)
	{
		FTDFluidGpuStampInput Stamp;
		Stamp.CenterPlaneCm = CenterPlaneCm;
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = DensityStrength;
		Stamp.VelocityStrength = 0.0f;
		Stamp.RippleImpulseCm = RippleImpulseCm;
		return Stamp;
	}

	void AddShearStamps(const FTDWaterGpuShearScene& Scene, FTDFluidGpuActorInput& InOutActor)
	{
		const int32 Resolution = GetWaterTier(Scene.Settings);
		const float CellSizeCm = static_cast<float>(ComputeWaterCellSize(Scene.Settings).X);
		const int32 DensityRows[] = { Resolution / 4 + Resolution / 16, Resolution / 2 - Resolution / 16 };
		const int32 DensityColumns[] = { Resolution / 4, Resolution / 2, 3 * Resolution / 4 };
		for (const int32 CellY : DensityRows)
		{
			for (const int32 CellX : DensityColumns)
			{
				InOutActor.Stamps.Add(MakeModuleWaterStamp(ComputeCellCenterPlaneCm(Scene.Settings, FIntPoint(CellX, CellY)), 3.0f * CellSizeCm, 0.8f, 0.0f));
			}
		}

		const FIntPoint RippleCells[] = { FIntPoint(Resolution / 3, Resolution / 6), FIntPoint(2 * Resolution / 3, 5 * Resolution / 6), FIntPoint(Resolution / 2, Resolution / 2 + Resolution / 8) };
		for (const FIntPoint& RippleCell : RippleCells)
		{
			InOutActor.Stamps.Add(MakeModuleWaterStamp(ComputeCellCenterPlaneCm(Scene.Settings, RippleCell), 4.0f * CellSizeCm, 0.0f, 2.0f));
		}
	}

	FIntPoint ComputeFieldShift(const FTDWaterGpuShearScene& Scene)
	{
		const int32 FieldDivisor = GetWaterFieldDivisor(Scene.Settings);
		return FIntPoint(Scene.ShiftCells.X / FieldDivisor, Scene.ShiftCells.Y / FieldDivisor);
	}

	FTDFluidGpuActorInput MakeShearActor(const FTDWaterGpuShearScene& Scene, int32 FrameIndex)
	{
		const int32 FieldResolution = GetWaterFieldResolution(Scene.Settings);
		const FVector2D CellSize = ComputeWaterCellSize(Scene.Settings);
		const FIntPoint FieldShift = ComputeFieldShift(Scene);
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = ModuleWaterActorKey;
		Actor.Settings = Scene.Settings;
		Actor.DeltaTimeSeconds = SixtyHertzStepSeconds;
		Actor.SimulationTimeSeconds = FrameIndex * static_cast<double>(SixtyHertzStepSeconds);
		if (FrameIndex >= 1)
		{
			Actor.WindowCenterPlaneCm = FVector2D(Scene.ShiftCells.X * CellSize.X, Scene.ShiftCells.Y * CellSize.Y);
			Actor.WaterFieldOrigin = FIntPoint(WrapFieldCoordinate(FieldShift.X, FieldResolution), WrapFieldCoordinate(FieldShift.Y, FieldResolution));
		}
		Actor.ShiftCells = FrameIndex == 1 ? Scene.ShiftCells : FIntPoint::ZeroValue;

		if (FrameIndex == 0)
		{
			for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
			{
				for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
				{
					AddShearGuideUpdate(FieldResolution, FIntPoint(FieldX, FieldY), FIntPoint::ZeroValue, Actor);
				}
			}
			AddShearStamps(Scene, Actor);
			return Actor;
		}

		if (FrameIndex != 1)
		{
			return Actor;
		}

		for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
		{
			for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
			{
				const FIntPoint FieldCell(FieldX, FieldY);
				if (!IsInsideGrid(FieldCell + FieldShift, FieldResolution))
				{
					AddShearGuideUpdate(FieldResolution, FieldCell, FieldShift, Actor);
				}
			}
		}
		return Actor;
	}

	constexpr int32 PressureGatherStampFrameIndex = 1;
	constexpr int32 PressureGatherShiftFrameIndex = 2;

	FTDFluidGpuActorInput MakePressureGatherActor(const FTDWaterGpuShearScene& Scene, int32 FrameIndex)
	{
		const int32 Resolution = GetWaterTier(Scene.Settings);
		const int32 FieldResolution = GetWaterFieldResolution(Scene.Settings);
		const FVector2D CellSize = ComputeWaterCellSize(Scene.Settings);
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = ModuleWaterActorKey;
		Actor.Settings = Scene.Settings;
		Actor.DeltaTimeSeconds = SixtyHertzStepSeconds;
		Actor.SimulationTimeSeconds = FrameIndex * static_cast<double>(SixtyHertzStepSeconds);
		if (FrameIndex >= PressureGatherShiftFrameIndex)
		{
			const FIntPoint FieldShift = ComputeFieldShift(Scene);
			Actor.WindowCenterPlaneCm = FVector2D(Scene.ShiftCells.X * CellSize.X, Scene.ShiftCells.Y * CellSize.Y);
			Actor.WaterFieldOrigin = FIntPoint(WrapFieldCoordinate(FieldShift.X, FieldResolution), WrapFieldCoordinate(FieldShift.Y, FieldResolution));
		}
		Actor.ShiftCells = FrameIndex == PressureGatherShiftFrameIndex ? Scene.ShiftCells : FIntPoint::ZeroValue;
		if (FrameIndex == PressureGatherStampFrameIndex)
		{
			FTDFluidGpuStampInput& Jet = Actor.Stamps.Add_GetRef(MakeModuleWaterStamp(ComputeCellCenterPlaneCm(Scene.Settings, FIntPoint(Resolution / 2, Resolution / 2)), 30.0f * static_cast<float>(CellSize.X), 0.5f, 0.0f));
			Jet.VelocityPlaneCmPerSec = FVector2D(300.0, 120.0);
			Jet.VelocityStrength = 1.0f;
			return Actor;
		}
		if (FrameIndex != 0)
		{
			return Actor;
		}

		for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
		{
			for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
			{
				FTDFluidGpuWaterGuideCellUpdate& GuideUpdate = Actor.WaterGuideUpdates.AddDefaulted_GetRef();
				GuideUpdate.FieldCell = FIntPoint(FieldX, FieldY);
				GuideUpdate.SurfaceHeightCm = ModuleWaterSurfaceHeightCm;
			}
		}
		return Actor;
	}

	struct FTDWaterGpuShiftComparison
	{
		int32 KeptCellCount = 0;
		int32 KeptMismatchCount = 0;
		int32 IncomingCellCount = 0;
		int32 IncomingMismatchCount = 0;
	};

	FTDWaterGpuShiftComparison CompareShiftedSlices(const FTDWaterGpuTextureSlice& Before, const FTDWaterGpuTextureSlice& After, const FIntPoint& ShiftCells, TFunctionRef<bool(const FIntPoint&)> IsIncomingValueExpected)
	{
		FTDWaterGpuShiftComparison Comparison;
		if (!IsSliceReadable(Before) || !IsSliceReadable(After) || Before.Resolution != After.Resolution)
		{
			Comparison.KeptMismatchCount = -1;
			return Comparison;
		}

		const int32 Resolution = After.Resolution;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FIntPoint Cell(CellX, CellY);
				const FIntPoint SourceCell = Cell + ShiftCells;
				if (!IsInsideGrid(SourceCell, Resolution))
				{
					++Comparison.IncomingCellCount;
					Comparison.IncomingMismatchCount += IsIncomingValueExpected(Cell) ? 0 : 1;
					continue;
				}

				++Comparison.KeptCellCount;
				Comparison.KeptMismatchCount += AreSliceTexelsBitIdentical(After, Cell, Before, SourceCell) ? 0 : 1;
			}
		}
		return Comparison;
	}

	bool IsSliceTexelZero(const FTDWaterGpuTextureSlice& Slice, const FIntPoint& Cell, int32 ChannelCount)
	{
		for (int32 Channel = 0; Channel < ChannelCount; ++Channel)
		{
			if (ReadSliceChannel(Slice, Cell.X, Cell.Y, Channel) != 0.0f)
			{
				return false;
			}
		}
		return true;
	}

	double FindSliceMaximumMagnitude(const FTDWaterGpuTextureSlice& Slice, int32 Channel)
	{
		double MaximumMagnitude = 0.0;
		if (!IsSliceReadable(Slice))
		{
			return MaximumMagnitude;
		}

		for (int32 CellY = 0; CellY < Slice.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Slice.Resolution; ++CellX)
			{
				MaximumMagnitude = FMath::Max(MaximumMagnitude, FMath::Abs(static_cast<double>(ReadSliceChannel(Slice, CellX, CellY, Channel))));
			}
		}
		return MaximumMagnitude;
	}

	FIntPoint MakeLogicalFieldTexel(const FIntPoint& FieldCell, const FIntPoint& FieldOrigin, int32 FieldResolution)
	{
		return FIntPoint(WrapFieldCoordinate(FieldCell.X + FieldOrigin.X, FieldResolution), WrapFieldCoordinate(FieldCell.Y + FieldOrigin.Y, FieldResolution));
	}

	struct FTDWaterGpuFieldComparison
	{
		int32 KeptMismatchCount = 0;
		int32 BandMismatchCount = 0;
		int32 BandCellCount = 0;
	};

	FTDWaterGpuFieldComparison CompareShiftedFields(const FTDWaterGpuStateSnapshot& Before, const FTDWaterGpuStateSnapshot& After, const FIntPoint& FieldShift, int32 FieldResolution)
	{
		FTDWaterGpuFieldComparison Comparison;
		const FTDWaterGpuTextureSlice& GuideBefore = GetSnapshotTexture(Before, ETDWaterGpuStateTexture::GuideField);
		const FTDWaterGpuTextureSlice& GuideAfter = GetSnapshotTexture(After, ETDWaterGpuStateTexture::GuideField);
		const FTDWaterGpuTextureSlice& SurfaceBefore = GetSnapshotTexture(Before, ETDWaterGpuStateTexture::SurfaceHeightField);
		const FTDWaterGpuTextureSlice& SurfaceAfter = GetSnapshotTexture(After, ETDWaterGpuStateTexture::SurfaceHeightField);
		const FTDWaterGpuTextureSlice& TerrainBefore = GetSnapshotTexture(Before, ETDWaterGpuStateTexture::TerrainField);
		const FTDWaterGpuTextureSlice& TerrainAfter = GetSnapshotTexture(After, ETDWaterGpuStateTexture::TerrainField);
		const bool bHasAllFields = IsSliceReadable(GuideBefore) && IsSliceReadable(GuideAfter) && IsSliceReadable(SurfaceBefore) && IsSliceReadable(SurfaceAfter) && IsSliceReadable(TerrainBefore) && IsSliceReadable(TerrainAfter);
		if (!bHasAllFields || GuideAfter.Resolution != FieldResolution)
		{
			Comparison.KeptMismatchCount = -1;
			return Comparison;
		}

		const FIntPoint OriginBefore = Before.WaterParameters.FieldOrigin;
		const FIntPoint OriginAfter = After.WaterParameters.FieldOrigin;
		for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
		{
			for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
			{
				const FIntPoint FieldCell(FieldX, FieldY);
				const FIntPoint TexelAfter = MakeLogicalFieldTexel(FieldCell, OriginAfter, FieldResolution);
				const FIntPoint SourceCell = FieldCell + FieldShift;
				if (!IsInsideGrid(SourceCell, FieldResolution))
				{
					++Comparison.BandCellCount;
					const float ExpectedSpeed = ComputeShearFlowSpeed(FieldY + FieldShift.Y, FieldResolution);
					const bool bMatchesBand = ReadSliceChannel(GuideAfter, TexelAfter.X, TexelAfter.Y, 0) == ExpectedSpeed
						&& ReadSliceChannel(GuideAfter, TexelAfter.X, TexelAfter.Y, 1) == 0.0f
						&& ReadSliceChannel(GuideAfter, TexelAfter.X, TexelAfter.Y, 2) == 1.0f
						&& ReadSliceChannel(SurfaceAfter, TexelAfter.X, TexelAfter.Y, 0) == ModuleWaterSurfaceHeightCm;
					Comparison.BandMismatchCount += bMatchesBand ? 0 : 1;
					continue;
				}

				const FIntPoint TexelBefore = MakeLogicalFieldTexel(SourceCell, OriginBefore, FieldResolution);
				const bool bIsKeptIdentical = AreSliceTexelsBitIdentical(GuideAfter, TexelAfter, GuideBefore, TexelBefore)
					&& AreSliceTexelsBitIdentical(SurfaceAfter, TexelAfter, SurfaceBefore, TexelBefore)
					&& AreSliceTexelsBitIdentical(TerrainAfter, TexelAfter, TerrainBefore, TexelBefore);
				Comparison.KeptMismatchCount += bIsKeptIdentical ? 0 : 1;
			}
		}
		return Comparison;
	}

	struct FTDWaterGpuFineSystem
	{
		bool bIsValid = false;
		int32 Resolution = 0;
		FVector2D RelativeCellSpacing = FVector2D::ZeroVector;
		FVector2D AxisWeights = FVector2D::ZeroVector;
		TArray<uint8> FluidCells;
		TArray<uint8> OpenEdgeCells;
		TArray<FVector2D> Velocity;
		TArray<double> Divergence;
	};

	bool IsSystemFluid(const FTDWaterGpuFineSystem& System, const FIntPoint& Cell)
	{
		return IsInsideGrid(Cell, System.Resolution) && System.FluidCells[Cell.Y * System.Resolution + Cell.X] != 0;
	}

	bool IsSystemOpenEdge(const FTDWaterGpuFineSystem& System, const FIntPoint& Cell)
	{
		return IsInsideGrid(Cell, System.Resolution) && System.OpenEdgeCells[Cell.Y * System.Resolution + Cell.X] != 0;
	}

	double ReadSystemFaceVelocity(const FTDWaterGpuFineSystem& System, const FIntPoint& Cell, const FIntPoint& Offset, int32 Axis)
	{
		const FIntPoint Neighbor = Cell + Offset;
		if (IsInsideGrid(Neighbor, System.Resolution))
		{
			return IsSystemFluid(System, Neighbor) ? System.Velocity[Neighbor.Y * System.Resolution + Neighbor.X][Axis] : 0.0;
		}

		return IsSystemOpenEdge(System, Cell) ? System.Velocity[Cell.Y * System.Resolution + Cell.X][Axis] : 0.0;
	}

	FTDWaterGpuFineSystem BuildFineSystem(const FTDWaterGpuRunResult& Run, const FTDWaterGpuTextureSlice& VelocitySlice)
	{
		FTDWaterGpuFineSystem System;
		const FTDWaterGpuPressureProbeCapture& Probe = Run.Probe;
		const int32 Resolution = Run.WaterKey.ResolutionTier;
		const int32 FieldDivisor = FMath::Max(Run.WaterParameters.FieldDivisor, 1);
		const int32 FieldResolution = Probe.SurfaceHeightField.Resolution;
		if (!Probe.bIsValid || !IsSliceReadable(VelocitySlice) || VelocitySlice.Resolution != Resolution || Probe.CompositeSdf.Resolution != Resolution || FieldResolution * FieldDivisor != Resolution)
		{
			return System;
		}

		System.Resolution = Resolution;
		System.RelativeCellSpacing = FVector2D(Run.WaterActorParameters.RelativeCellSpacing.X, Run.WaterActorParameters.RelativeCellSpacing.Y);
		System.AxisWeights = FVector2D(Run.WaterActorParameters.InvSpacingXSquared, Run.WaterActorParameters.InvSpacingYSquared);
		const int32 CellCount = Resolution * Resolution;
		System.FluidCells.SetNumZeroed(CellCount);
		System.OpenEdgeCells.SetNumZeroed(CellCount);
		System.Velocity.SetNumZeroed(CellCount);
		System.Divergence.SetNumZeroed(CellCount);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const int32 CellIndex = CellY * Resolution + CellX;
				System.FluidCells[CellIndex] = ReadSliceChannel(Probe.CompositeSdf, CellX, CellY, 0) > 0.0f ? 1 : 0;
				System.Velocity[CellIndex] = FVector2D(ReadSliceChannel(VelocitySlice, CellX, CellY, 0), ReadSliceChannel(VelocitySlice, CellX, CellY, 1));
				const bool bIsEdge = CellX == 0 || CellY == 0 || CellX == Resolution - 1 || CellY == Resolution - 1;
				if (Probe.OpenBorderFlag == 0 || !bIsEdge || System.FluidCells[CellIndex] == 0)
				{
					continue;
				}

				const FIntPoint FieldTexel = MakeLogicalFieldTexel(FIntPoint(CellX / FieldDivisor, CellY / FieldDivisor), Run.WaterParameters.FieldOrigin, FieldResolution);
				const float SurfaceHeight = ReadSliceChannel(Probe.SurfaceHeightField, FieldTexel.X, FieldTexel.Y, 0);
				System.OpenEdgeCells[CellIndex] = SurfaceHeight > TDFluidGpuSolverConstants::WaterNoSurfaceHeightThresholdCm ? 1 : 0;
			}
		}

		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FIntPoint Cell(CellX, CellY);
				if (!IsSystemFluid(System, Cell))
				{
					continue;
				}

				const double VelocityRight = ReadSystemFaceVelocity(System, Cell, FIntPoint(1, 0), 0);
				const double VelocityLeft = ReadSystemFaceVelocity(System, Cell, FIntPoint(-1, 0), 0);
				const double VelocityUp = ReadSystemFaceVelocity(System, Cell, FIntPoint(0, 1), 1);
				const double VelocityDown = ReadSystemFaceVelocity(System, Cell, FIntPoint(0, -1), 1);
				System.Divergence[CellY * Resolution + CellX] = (VelocityRight - VelocityLeft) / (2.0 * System.RelativeCellSpacing.X) + (VelocityUp - VelocityDown) / (2.0 * System.RelativeCellSpacing.Y);
			}
		}
		System.bIsValid = true;
		return System;
	}

	struct FTDWaterGpuHalfResidual
	{
		bool bIsValid = false;
		double RhsNorm = 0.0;
		double ResidualNorm = 0.0;
		double IncompatibleNorm = 0.0;
		int32 ActiveCellCount = 0;
		int32 AnchoredComponentCount = 0;
		int32 ClosedComponentCount = 0;
	};

	FTDWaterGpuHalfResidual ComputeHalfResidual(const FTDWaterGpuFineSystem& System, const FTDWaterGpuTextureSlice& HalfPressure)
	{
		FTDWaterGpuHalfResidual Residual;
		const int32 Resolution = System.Resolution;
		const int32 HalfResolution = Resolution / 2;
		if (!System.bIsValid || !IsSliceReadable(HalfPressure) || HalfPressure.Resolution != HalfResolution)
		{
			return Residual;
		}

		const int32 HalfCellCount = HalfResolution * HalfResolution;
		const FVector2D HalfWeights = 0.25 * System.AxisWeights;
		TArray<double> Rhs;
		TArray<FVector2D> Apertures;
		TArray<FVector2D> OpenBoundary;
		TArray<uint8> ActiveCells;
		Rhs.SetNumZeroed(HalfCellCount);
		Apertures.SetNumZeroed(HalfCellCount);
		OpenBoundary.SetNumZeroed(HalfCellCount);
		ActiveCells.SetNumZeroed(HalfCellCount);
		for (int32 HalfY = 0; HalfY < HalfResolution; ++HalfY)
		{
			for (int32 HalfX = 0; HalfX < HalfResolution; ++HalfX)
			{
				const int32 HalfIndex = HalfY * HalfResolution + HalfX;
				const FIntPoint ChildOrigin(2 * HalfX, 2 * HalfY);
				for (int32 ChildY = 0; ChildY < 2; ++ChildY)
				{
					for (int32 ChildX = 0; ChildX < 2; ++ChildX)
					{
						const FIntPoint Child = ChildOrigin + FIntPoint(ChildX, ChildY);
						if (!IsSystemFluid(System, Child))
						{
							continue;
						}

						ActiveCells[HalfIndex] = 1;
						Rhs[HalfIndex] += 0.25 * System.Divergence[Child.Y * Resolution + Child.X];
						if (IsSystemOpenEdge(System, Child))
						{
							OpenBoundary[HalfIndex].X += (Child.X == 0 || Child.X == Resolution - 1) ? 0.5 : 0.0;
							OpenBoundary[HalfIndex].Y += (Child.Y == 0 || Child.Y == Resolution - 1) ? 0.5 : 0.0;
						}
					}
				}

				const auto IsFaceOpenX = [&System](const FIntPoint& Child) { return IsSystemFluid(System, Child) && IsSystemFluid(System, Child + FIntPoint(1, 0)) ? 1.0 : 0.0; };
				const auto IsFaceOpenY = [&System](const FIntPoint& Child) { return IsSystemFluid(System, Child) && IsSystemFluid(System, Child + FIntPoint(0, 1)) ? 1.0 : 0.0; };
				Apertures[HalfIndex].X = 0.5 * (IsFaceOpenX(ChildOrigin + FIntPoint(1, 0)) + IsFaceOpenX(ChildOrigin + FIntPoint(1, 1)));
				Apertures[HalfIndex].Y = 0.5 * (IsFaceOpenY(ChildOrigin + FIntPoint(0, 1)) + IsFaceOpenY(ChildOrigin + FIntPoint(1, 1)));
			}
		}

		const auto ReadHalfPressure = [&HalfPressure](int32 HalfX, int32 HalfY) { return static_cast<double>(ReadSliceChannel(HalfPressure, HalfX, HalfY, 0)); };
		TArray<double> HalfResidualValues;
		HalfResidualValues.SetNumZeroed(HalfCellCount);
		for (int32 HalfY = 0; HalfY < HalfResolution; ++HalfY)
		{
			for (int32 HalfX = 0; HalfX < HalfResolution; ++HalfX)
			{
				const int32 HalfIndex = HalfY * HalfResolution + HalfX;
				if (ActiveCells[HalfIndex] == 0)
				{
					continue;
				}

				const double CenterPressure = ReadHalfPressure(HalfX, HalfY);
				double AppliedOperator = 0.0;
				if (HalfX + 1 < HalfResolution)
				{
					AppliedOperator += HalfWeights.X * Apertures[HalfIndex].X * (ReadHalfPressure(HalfX + 1, HalfY) - CenterPressure);
				}
				if (HalfX > 0)
				{
					AppliedOperator += HalfWeights.X * Apertures[HalfIndex - 1].X * (ReadHalfPressure(HalfX - 1, HalfY) - CenterPressure);
				}
				if (HalfY + 1 < HalfResolution)
				{
					AppliedOperator += HalfWeights.Y * Apertures[HalfIndex].Y * (ReadHalfPressure(HalfX, HalfY + 1) - CenterPressure);
				}
				if (HalfY > 0)
				{
					AppliedOperator += HalfWeights.Y * Apertures[HalfIndex - HalfResolution].Y * (ReadHalfPressure(HalfX, HalfY - 1) - CenterPressure);
				}
				AppliedOperator -= WaterHalfPressureDirichletScaleLevelOne * (OpenBoundary[HalfIndex].X * HalfWeights.X + OpenBoundary[HalfIndex].Y * HalfWeights.Y) * CenterPressure;
				HalfResidualValues[HalfIndex] = Rhs[HalfIndex] - AppliedOperator;
			}
		}

		TArray<int32> ComponentIds;
		ComponentIds.Init(INDEX_NONE, HalfCellCount);
		TArray<int32> Queue;
		TArray<int32> ComponentCells;
		double RhsSquares = 0.0;
		double ResidualSquares = 0.0;
		double IncompatibleSquares = 0.0;
		for (int32 SeedIndex = 0; SeedIndex < HalfCellCount; ++SeedIndex)
		{
			if (ActiveCells[SeedIndex] == 0 || ComponentIds[SeedIndex] != INDEX_NONE)
			{
				continue;
			}

			const int32 ComponentId = Residual.AnchoredComponentCount + Residual.ClosedComponentCount;
			bool bIsAnchored = false;
			Queue.Reset();
			Queue.Add(SeedIndex);
			ComponentIds[SeedIndex] = ComponentId;
			for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
			{
				const int32 HalfIndex = Queue[QueueIndex];
				const int32 HalfX = HalfIndex % HalfResolution;
				const int32 HalfY = HalfIndex / HalfResolution;
				bIsAnchored |= OpenBoundary[HalfIndex].X > 0.0 || OpenBoundary[HalfIndex].Y > 0.0;
				const int32 NeighborIndices[] = { HalfX + 1 < HalfResolution && Apertures[HalfIndex].X > 0.0 ? HalfIndex + 1 : INDEX_NONE, HalfX > 0 && Apertures[HalfIndex - 1].X > 0.0 ? HalfIndex - 1 : INDEX_NONE, HalfY + 1 < HalfResolution && Apertures[HalfIndex].Y > 0.0 ? HalfIndex + HalfResolution : INDEX_NONE, HalfY > 0 && Apertures[HalfIndex - HalfResolution].Y > 0.0 ? HalfIndex - HalfResolution : INDEX_NONE };
				for (const int32 NeighborIndex : NeighborIndices)
				{
					if (NeighborIndex != INDEX_NONE && ActiveCells[NeighborIndex] != 0 && ComponentIds[NeighborIndex] == INDEX_NONE)
					{
						ComponentIds[NeighborIndex] = ComponentId;
						Queue.Add(NeighborIndex);
					}
				}
			}

			double RhsMean = 0.0;
			double ResidualMean = 0.0;
			if (!bIsAnchored)
			{
				for (const int32 HalfIndex : Queue)
				{
					RhsMean += Rhs[HalfIndex];
					ResidualMean += HalfResidualValues[HalfIndex];
				}
				RhsMean /= Queue.Num();
				ResidualMean /= Queue.Num();
				IncompatibleSquares += Queue.Num() * FMath::Square(RhsMean);
			}

			for (const int32 HalfIndex : Queue)
			{
				RhsSquares += FMath::Square(Rhs[HalfIndex] - RhsMean);
				ResidualSquares += FMath::Square(HalfResidualValues[HalfIndex] - ResidualMean);
			}
			Residual.ActiveCellCount += Queue.Num();
			Residual.AnchoredComponentCount += bIsAnchored ? 1 : 0;
			Residual.ClosedComponentCount += bIsAnchored ? 0 : 1;
		}

		Residual.RhsNorm = FMath::Sqrt(RhsSquares);
		Residual.ResidualNorm = FMath::Sqrt(ResidualSquares);
		Residual.IncompatibleNorm = FMath::Sqrt(IncompatibleSquares);
		Residual.bIsValid = Residual.ActiveCellCount > 0;
		return Residual;
	}

	struct FTDWaterGpuReferenceSolve
	{
		bool bHasConverged = false;
		int32 IterationCount = 0;
		double RelativeResidual = 0.0;
		TArray<uint8> SolvedCells;
		TArray<double> Pressure;
	};

	FTDWaterGpuReferenceSolve SolveFineReferencePressure(const FTDWaterGpuFineSystem& System, int32 MaximumIterationCount, double RelativeResidualTarget)
	{
		FTDWaterGpuReferenceSolve Solve;
		const int32 Resolution = System.Resolution;
		const int32 CellCount = Resolution * Resolution;
		Solve.SolvedCells.SetNumZeroed(CellCount);
		Solve.Pressure.SetNumZeroed(CellCount);
		if (!System.bIsValid)
		{
			return Solve;
		}

		const FIntPoint Offsets[] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
		TArray<int32> Queue;
		TArray<uint8> VisitedCells;
		VisitedCells.SetNumZeroed(CellCount);
		for (int32 SeedIndex = 0; SeedIndex < CellCount; ++SeedIndex)
		{
			if (System.FluidCells[SeedIndex] == 0 || VisitedCells[SeedIndex] != 0)
			{
				continue;
			}

			bool bIsAnchored = false;
			Queue.Reset();
			Queue.Add(SeedIndex);
			VisitedCells[SeedIndex] = 1;
			for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
			{
				const FIntPoint Cell(Queue[QueueIndex] % Resolution, Queue[QueueIndex] / Resolution);
				bIsAnchored |= IsSystemOpenEdge(System, Cell);
				for (const FIntPoint& Offset : Offsets)
				{
					const FIntPoint Neighbor = Cell + Offset;
					const int32 NeighborIndex = Neighbor.Y * Resolution + Neighbor.X;
					if (IsSystemFluid(System, Neighbor) && VisitedCells[NeighborIndex] == 0)
					{
						VisitedCells[NeighborIndex] = 1;
						Queue.Add(NeighborIndex);
					}
				}
			}

			for (const int32 CellIndex : Queue)
			{
				Solve.SolvedCells[CellIndex] = bIsAnchored ? 1 : 0;
			}
		}

		TArray<int32> UnknownByCell;
		UnknownByCell.Init(INDEX_NONE, CellCount);
		TArray<int32> CellByUnknown;
		for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
		{
			if (Solve.SolvedCells[CellIndex] != 0)
			{
				UnknownByCell[CellIndex] = CellByUnknown.Add(CellIndex);
			}
		}

		const int32 UnknownCount = CellByUnknown.Num();
		if (UnknownCount == 0)
		{
			return Solve;
		}

		TArray<double> Diagonal;
		TArray<int32> NeighborUnknowns;
		TArray<double> NeighborWeights;
		TArray<double> RightHandSide;
		Diagonal.SetNumZeroed(UnknownCount);
		NeighborUnknowns.Init(INDEX_NONE, UnknownCount * 4);
		NeighborWeights.SetNumZeroed(UnknownCount * 4);
		RightHandSide.SetNumZeroed(UnknownCount);
		for (int32 Unknown = 0; Unknown < UnknownCount; ++Unknown)
		{
			const int32 CellIndex = CellByUnknown[Unknown];
			const FIntPoint Cell(CellIndex % Resolution, CellIndex / Resolution);
			RightHandSide[Unknown] = -System.Divergence[CellIndex];
			for (int32 OffsetIndex = 0; OffsetIndex < 4; ++OffsetIndex)
			{
				const FIntPoint Neighbor = Cell + Offsets[OffsetIndex];
				const double AxisWeight = Offsets[OffsetIndex].X != 0 ? System.AxisWeights.X : System.AxisWeights.Y;
				if (!IsInsideGrid(Neighbor, Resolution))
				{
					Diagonal[Unknown] += IsSystemOpenEdge(System, Cell) ? AxisWeight : 0.0;
					continue;
				}

				if (!IsSystemFluid(System, Neighbor))
				{
					continue;
				}

				Diagonal[Unknown] += AxisWeight;
				NeighborUnknowns[Unknown * 4 + OffsetIndex] = UnknownByCell[Neighbor.Y * Resolution + Neighbor.X];
				NeighborWeights[Unknown * 4 + OffsetIndex] = AxisWeight;
			}
		}

		const auto ApplyOperator = [&Diagonal, &NeighborUnknowns, &NeighborWeights, UnknownCount](const TArray<double>& Input, TArray<double>& Output)
		{
			for (int32 Unknown = 0; Unknown < UnknownCount; ++Unknown)
			{
				double Value = Diagonal[Unknown] * Input[Unknown];
				for (int32 OffsetIndex = 0; OffsetIndex < 4; ++OffsetIndex)
				{
					const int32 NeighborUnknown = NeighborUnknowns[Unknown * 4 + OffsetIndex];
					if (NeighborUnknown != INDEX_NONE)
					{
						Value -= NeighborWeights[Unknown * 4 + OffsetIndex] * Input[NeighborUnknown];
					}
				}
				Output[Unknown] = Value;
			}
		};
		const auto DotProduct = [UnknownCount](const TArray<double>& Left, const TArray<double>& Right)
		{
			double Sum = 0.0;
			for (int32 Unknown = 0; Unknown < UnknownCount; ++Unknown)
			{
				Sum += Left[Unknown] * Right[Unknown];
			}
			return Sum;
		};

		TArray<double> Solution;
		TArray<double> Remainder = RightHandSide;
		TArray<double> SearchDirection = RightHandSide;
		TArray<double> AppliedDirection;
		Solution.SetNumZeroed(UnknownCount);
		AppliedDirection.SetNumZeroed(UnknownCount);
		const double RightHandSideNorm = FMath::Sqrt(DotProduct(RightHandSide, RightHandSide));
		if (RightHandSideNorm <= 0.0)
		{
			Solve.bHasConverged = true;
			return Solve;
		}

		double RemainderSquares = DotProduct(Remainder, Remainder);
		for (int32 Iteration = 0; Iteration < MaximumIterationCount; ++Iteration)
		{
			if (FMath::Sqrt(RemainderSquares) / RightHandSideNorm <= 0.5 * RelativeResidualTarget)
			{
				break;
			}

			ApplyOperator(SearchDirection, AppliedDirection);
			const double Curvature = DotProduct(SearchDirection, AppliedDirection);
			if (Curvature <= 0.0)
			{
				break;
			}

			const double StepLength = RemainderSquares / Curvature;
			for (int32 Unknown = 0; Unknown < UnknownCount; ++Unknown)
			{
				Solution[Unknown] += StepLength * SearchDirection[Unknown];
				Remainder[Unknown] -= StepLength * AppliedDirection[Unknown];
			}

			const double NextRemainderSquares = DotProduct(Remainder, Remainder);
			const double DirectionBlend = NextRemainderSquares / RemainderSquares;
			for (int32 Unknown = 0; Unknown < UnknownCount; ++Unknown)
			{
				SearchDirection[Unknown] = Remainder[Unknown] + DirectionBlend * SearchDirection[Unknown];
			}
			RemainderSquares = NextRemainderSquares;
			Solve.IterationCount = Iteration + 1;
		}

		ApplyOperator(Solution, AppliedDirection);
		double TrueResidualSquares = 0.0;
		for (int32 Unknown = 0; Unknown < UnknownCount; ++Unknown)
		{
			TrueResidualSquares += FMath::Square(RightHandSide[Unknown] - AppliedDirection[Unknown]);
			Solve.Pressure[CellByUnknown[Unknown]] = Solution[Unknown];
		}
		Solve.RelativeResidual = FMath::Sqrt(TrueResidualSquares) / RightHandSideNorm;
		Solve.bHasConverged = Solve.RelativeResidual <= RelativeResidualTarget;
		return Solve;
	}

	bool HasSolidWithinDistance(const FTDWaterGpuFineSystem& System, const FIntPoint& Cell, int32 Distance)
	{
		for (int32 OffsetY = -Distance; OffsetY <= Distance; ++OffsetY)
		{
			for (int32 OffsetX = -Distance; OffsetX <= Distance; ++OffsetX)
			{
				const FIntPoint Neighbor = Cell + FIntPoint(OffsetX, OffsetY);
				if (IsInsideGrid(Neighbor, System.Resolution) && !IsSystemFluid(System, Neighbor))
				{
					return true;
				}
			}
		}
		return false;
	}

	struct FTDWaterGpuBoundaryError
	{
		double BandRelativeError = 0.0;
		double InteriorRelativeError = 0.0;
		int32 BandCellCount = 0;
		int32 InteriorCellCount = 0;
	};

	FTDWaterGpuBoundaryError MeasureBoundaryError(const FTDWaterGpuFineSystem& System, const FTDWaterGpuReferenceSolve& Reference, const FTDWaterGpuTextureSlice& PressureFull, int32 BandWidthCells, int32 SolidMarginCells)
	{
		FTDWaterGpuBoundaryError Error;
		double BandDifferenceSquares = 0.0;
		double BandReferenceSquares = 0.0;
		double InteriorDifferenceSquares = 0.0;
		double InteriorReferenceSquares = 0.0;
		const int32 Resolution = System.Resolution;
		if (!IsSliceReadable(PressureFull) || PressureFull.Resolution != Resolution)
		{
			Error.BandRelativeError = TNumericLimits<double>::Max();
			Error.InteriorRelativeError = TNumericLimits<double>::Max();
			return Error;
		}

		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const int32 CellIndex = CellY * Resolution + CellX;
				if (Reference.SolvedCells[CellIndex] == 0)
				{
					continue;
				}

				const double ReferencePressure = Reference.Pressure[CellIndex];
				const double Difference = static_cast<double>(ReadSliceChannel(PressureFull, CellX, CellY, 0)) - ReferencePressure;
				const bool bIsInBand = CellX < BandWidthCells || CellX >= Resolution - BandWidthCells;
				if (bIsInBand)
				{
					BandDifferenceSquares += Difference * Difference;
					BandReferenceSquares += ReferencePressure * ReferencePressure;
					++Error.BandCellCount;
					continue;
				}

				if (HasSolidWithinDistance(System, FIntPoint(CellX, CellY), SolidMarginCells))
				{
					continue;
				}

				InteriorDifferenceSquares += Difference * Difference;
				InteriorReferenceSquares += ReferencePressure * ReferencePressure;
				++Error.InteriorCellCount;
			}
		}

		Error.BandRelativeError = FMath::Sqrt(BandDifferenceSquares) / FMath::Max(FMath::Sqrt(BandReferenceSquares), UE_DOUBLE_SMALL_NUMBER);
		Error.InteriorRelativeError = FMath::Sqrt(InteriorDifferenceSquares) / FMath::Max(FMath::Sqrt(InteriorReferenceSquares), UE_DOUBLE_SMALL_NUMBER);
		return Error;
	}

	struct FTDWaterGpuFlux
	{
		double Inflow = 0.0;
		double Outflow = 0.0;
	};

	FTDWaterGpuFlux MeasureChannelFlux(const FTDWaterGpuFineSystem& System, const FTDWaterGpuTextureSlice& ProjectedVelocity)
	{
		FTDWaterGpuFlux Flux;
		const int32 Resolution = System.Resolution;
		if (!IsSliceReadable(ProjectedVelocity) || ProjectedVelocity.Resolution != Resolution)
		{
			return Flux;
		}

		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			if (IsSystemFluid(System, FIntPoint(0, CellY)))
			{
				Flux.Inflow += ReadSliceChannel(ProjectedVelocity, 0, CellY, 0);
			}
			if (IsSystemFluid(System, FIntPoint(Resolution - 1, CellY)))
			{
				Flux.Outflow += ReadSliceChannel(ProjectedVelocity, Resolution - 1, CellY, 0);
			}
		}
		return Flux;
	}

	TArray<double> ReadPressureSlice(const FTDWaterGpuTextureSlice& Slice, int32 Resolution)
	{
		TArray<double> Pressure;
		if (!IsSliceReadable(Slice) || Slice.Resolution != Resolution)
		{
			return Pressure;
		}

		Pressure.SetNumZeroed(Resolution * Resolution);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				Pressure[CellY * Resolution + CellX] = ReadSliceChannel(Slice, CellX, CellY, 0);
			}
		}
		return Pressure;
	}

	FVector2D ComputeFloorProjectedVelocity(const FTDWaterGpuFineSystem& System, const FTDWaterGpuTextureSlice& CompositeSdf, const TArray<double>& Pressure, const FIntPoint& Cell, double BoundaryBandCm, bool bEnforcesObstacleBoundary)
	{
		constexpr double NormalLengthSquaredFloor = 1.0e-8;
		const int32 Resolution = System.Resolution;
		const double CenterPressure = Pressure[Cell.Y * Resolution + Cell.X];
		const double GhostPressure = IsSystemOpenEdge(System, Cell) ? 0.0 : CenterPressure;
		const auto LoadNeighborPressure = [&](const FIntPoint& Neighbor)
		{
			if (!IsInsideGrid(Neighbor, Resolution))
			{
				return GhostPressure;
			}
			return IsSystemFluid(System, Neighbor) ? Pressure[Neighbor.Y * Resolution + Neighbor.X] : CenterPressure;
		};

		FVector2D Velocity = System.Velocity[Cell.Y * Resolution + Cell.X];
		Velocity.X -= (LoadNeighborPressure(Cell + FIntPoint(1, 0)) - LoadNeighborPressure(Cell - FIntPoint(1, 0))) / (2.0 * System.RelativeCellSpacing.X);
		Velocity.Y -= (LoadNeighborPressure(Cell + FIntPoint(0, 1)) - LoadNeighborPressure(Cell - FIntPoint(0, 1))) / (2.0 * System.RelativeCellSpacing.Y);
		if (!bEnforcesObstacleBoundary)
		{
			return Velocity;
		}

		const double SignedDistance = ReadSliceChannel(CompositeSdf, Cell.X, Cell.Y, 0);
		if (SignedDistance <= 0.0)
		{
			return FVector2D::ZeroVector;
		}

		if (SignedDistance >= BoundaryBandCm)
		{
			return Velocity;
		}

		const FIntPoint MinusCell(FMath::Max(Cell.X - 1, 0), FMath::Max(Cell.Y - 1, 0));
		const FIntPoint PlusCell(FMath::Min(Cell.X + 1, Resolution - 1), FMath::Min(Cell.Y + 1, Resolution - 1));
		FVector2D Normal(
			(ReadSliceChannel(CompositeSdf, PlusCell.X, Cell.Y, 0) - ReadSliceChannel(CompositeSdf, MinusCell.X, Cell.Y, 0)) / (static_cast<double>(PlusCell.X - MinusCell.X) * System.RelativeCellSpacing.X),
			(ReadSliceChannel(CompositeSdf, Cell.X, PlusCell.Y, 0) - ReadSliceChannel(CompositeSdf, Cell.X, MinusCell.Y, 0)) / (static_cast<double>(PlusCell.Y - MinusCell.Y) * System.RelativeCellSpacing.Y));
		const double NormalLengthSquared = Normal.SquaredLength();
		if (!(NormalLengthSquared > NormalLengthSquaredFloor))
		{
			return Velocity;
		}

		Normal /= FMath::Sqrt(NormalLengthSquared);
		const double NormalDot = Velocity | Normal;
		if (NormalDot < 0.0)
		{
			Velocity -= Normal * NormalDot;
		}
		return Velocity;
	}

	double MeasureFloorFluxImbalance(const FTDWaterGpuFineSystem& System, const FTDWaterGpuTextureSlice& CompositeSdf, const TArray<double>& Pressure, double BoundaryBandCm, bool bEnforcesObstacleBoundary)
	{
		const int32 Resolution = System.Resolution;
		if (!System.bIsValid || !IsSliceReadable(CompositeSdf) || CompositeSdf.Resolution != Resolution || Pressure.Num() != Resolution * Resolution)
		{
			return -1.0;
		}

		double Inflow = 0.0;
		double Outflow = 0.0;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			if (IsSystemFluid(System, FIntPoint(0, CellY)))
			{
				Inflow += ComputeFloorProjectedVelocity(System, CompositeSdf, Pressure, FIntPoint(0, CellY), BoundaryBandCm, bEnforcesObstacleBoundary).X;
			}
			if (IsSystemFluid(System, FIntPoint(Resolution - 1, CellY)))
			{
				Outflow += ComputeFloorProjectedVelocity(System, CompositeSdf, Pressure, FIntPoint(Resolution - 1, CellY), BoundaryBandCm, bEnforcesObstacleBoundary).X;
			}
		}
		return FMath::Abs(Inflow - Outflow) / FMath::Max(FMath::Abs(Inflow), UE_DOUBLE_SMALL_NUMBER);
	}

	FString MakeFluxFloorLine(const FTDWaterGpuFineSystem& System, const FTDWaterGpuReferenceSolve& Reference, const FTDWaterGpuTextureSlice& CompositeSdf, const FTDWaterGpuTextureSlice* HalfCycleSixteenPressure, double GpuImbalance, double BoundaryBandCm)
	{
		const double ReferenceImbalance = MeasureFloorFluxImbalance(System, CompositeSdf, Reference.Pressure, BoundaryBandCm, true);
		const double ReferenceImbalanceBeforeEnforce = MeasureFloorFluxImbalance(System, CompositeSdf, Reference.Pressure, BoundaryBandCm, false);
		const TArray<double> HalfPressure = HalfCycleSixteenPressure ? ReadPressureSlice(*HalfCycleSixteenPressure, System.Resolution) : TArray<double>();
		const double HalfImbalance = MeasureFloorFluxImbalance(System, CompositeSdf, HalfPressure, BoundaryBandCm, true);
		return FString::Printf(TEXT("TDFluidGpuWaterFluxFloor: reference_imbalance=%.6f reference_imbalance_before_enforce=%.6f half_c16_imbalance=%.6f gpu_imbalance=%.6f"), ReferenceImbalance, ReferenceImbalanceBeforeEnforce, HalfImbalance, GpuImbalance);
	}

	FTDWaterGpuChannelScene MakeRockChannelScene(bool bOpensWaterBorders)
	{
		FTDWaterGpuChannelScene Scene;
		Scene.Settings = MakeModuleWaterSettings(512, bOpensWaterBorders);
		Scene.RockRadiusCm = 216.0f;
		return Scene;
	}

	FTDWaterGpuChannelScene MakePillarChannelScene()
	{
		FTDWaterGpuChannelScene Scene;
		Scene.Settings = MakeModuleWaterSettings(512, false);
		Scene.bAddsPillarPatch = true;
		return Scene;
	}

	double ComputeHalfPressureGroupMean(const FTDWaterGpuTextureSlice& HalfPressure, bool bUsesGroupA)
	{
		const int32 Resolution = HalfPressure.Resolution * 2;
		const FIntPoint MinimumCell = GetPillarPatchMinimumCell(Resolution);
		const FIntPoint EndCell = GetPillarPatchEndCell(Resolution);
		double Sum = 0.0;
		int32 Count = 0;
		for (int32 CellY = MinimumCell.Y; CellY < EndCell.Y; ++CellY)
		{
			for (int32 CellX = MinimumCell.X; CellX < EndCell.X; ++CellX)
			{
				const FIntPoint Cell(CellX, CellY);
				const bool bIsGroupPillar = bUsesGroupA ? IsGroupAPillarCell(Cell) : IsGroupCPillarCell(Cell);
				if (!bIsGroupPillar)
				{
					continue;
				}

				Sum += ReadSliceChannel(HalfPressure, CellX / 2, CellY / 2, 0);
				++Count;
			}
		}
		return Count > 0 ? Sum / Count : 0.0;
	}

	double ComputeSliceRange(const FTDWaterGpuTextureSlice& Slice)
	{
		double Minimum = TNumericLimits<double>::Max();
		double Maximum = TNumericLimits<double>::Lowest();
		for (int32 CellY = 0; CellY < Slice.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Slice.Resolution; ++CellX)
			{
				const double Value = ReadSliceChannel(Slice, CellX, CellY, 0);
				Minimum = FMath::Min(Minimum, Value);
				Maximum = FMath::Max(Maximum, Value);
			}
		}
		return Maximum >= Minimum ? Maximum - Minimum : 0.0;
	}

	int32 CountSliceByteDifferences(const FTDWaterGpuTextureSlice& Left, const FTDWaterGpuTextureSlice& Right)
	{
		if (!IsSliceReadable(Left) || !IsSliceReadable(Right) || Left.Resolution != Right.Resolution || Left.Format != Right.Format)
		{
			return -1;
		}

		int32 DifferenceCount = 0;
		for (int32 CellY = 0; CellY < Left.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Left.Resolution; ++CellX)
			{
				DifferenceCount += AreSliceTexelsBitIdentical(Left, FIntPoint(CellX, CellY), Right, FIntPoint(CellX, CellY)) ? 0 : 1;
			}
		}
		return DifferenceCount;
	}

	const TCHAR* const OptimizationVariableNames[] = {
		TEXT("TD.FluidFX.WaterGpuWideOpenBorderScan"),
		TEXT("TD.FluidFX.WaterGpuSolidFirstReads"),
		TEXT("TD.FluidFX.WaterGpuSkipOpenPressureMean"),
		TEXT("TD.FluidFX.WaterGpuSkipRippleParityCopy"),
		TEXT("TD.FluidFX.WaterGpuSkipDryPressureTiles"),
		TEXT("TD.FluidFX.WaterGpuFusedPressureTail"),
		TEXT("TD.FluidFX.WaterGpuFusedPrestep"),
		TEXT("TD.FluidFX.WaterGpuSharedHalfApertures"),
		TEXT("TD.FluidFX.WaterGpuSkipInactiveMultigridTiles"),
	};
	constexpr float OptimizationStepSeconds[] = { 1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 144.0f, 0.1f };
	constexpr int32 OptimizationFrameCount = 24;
	constexpr int32 OptimizationShiftFrameIndex = 9;

	struct FTDWaterGpuOptimizationCase
	{
		const TCHAR* Label = TEXT("");
		int32 Resolution = 0;
		bool bOpensWaterBorders = true;
		bool bTreatsDryCellsAsSolid = true;
		float RippleWaveSpeedCmPerSec = 120.0f;
		bool bShiftsWindow = false;
		int32 PressureIterations = 0;
	};

	using FTDWaterGpuOptimizationVariableScopes = TArray<TUniquePtr<FTDWaterGpuConsoleVariableScope>>;

	FTDWaterGpuOptimizationVariableScopes ApplyOptimizationVariables(int32 Value)
	{
		FTDWaterGpuOptimizationVariableScopes Scopes;
		for (const TCHAR* VariableName : OptimizationVariableNames)
		{
			Scopes.Add(MakeUnique<FTDWaterGpuConsoleVariableScope>(VariableName, Value));
		}
		return Scopes;
	}

	bool AreOptimizationVariablesApplied(const FTDWaterGpuOptimizationVariableScopes& Scopes)
	{
		for (const TUniquePtr<FTDWaterGpuConsoleVariableScope>& Scope : Scopes)
		{
			if (!Scope->IsValid())
			{
				return false;
			}
		}
		return !Scopes.IsEmpty();
	}

	FTDWaterGpuChannelScene MakeOptimizationScene(const FTDWaterGpuOptimizationCase& Case)
	{
		FTDWaterGpuChannelScene Scene;
		Scene.Settings = MakeModuleWaterSettings(Case.Resolution, Case.bOpensWaterBorders);
		Scene.Settings.bTreatDryCellsAsSolid = Case.bTreatsDryCellsAsSolid;
		Scene.Settings.TerrainSlopeGravity = Case.bTreatsDryCellsAsSolid ? 0.0f : 300.0f;
		Scene.Settings.RippleWaveSpeedCmPerSec = Case.RippleWaveSpeedCmPerSec;
		Scene.Settings.VorticityStrength = 0.3f;
		Scene.RockRadiusCm = 216.0f;
		if (Case.PressureIterations > 0)
		{
			Scene.Settings.PressureIterations = Case.PressureIterations;
		}
		return Scene;
	}

	float GetOptimizationStepSeconds(int32 FrameIndex)
	{
		return OptimizationStepSeconds[FrameIndex % UE_ARRAY_COUNT(OptimizationStepSeconds)];
	}

	int32 ComputeOptimizationRippleSubstepCount(const FTDWaterGpuChannelScene& Scene, int32 FrameIndex)
	{
		const FVector2D CellSize = ComputeWaterCellSize(Scene.Settings);
		bool bReachedCap = false;
		float WaveSpeedScale = 1.0f;
		const float StepSeconds = FMath::Min(GetOptimizationStepSeconds(FrameIndex), TDFluidGpuSolverConstants::MaximumStepDeltaTimeSeconds);
		return FTDFluidGpuBatch::ComputeWaterRippleSubstepCount(Scene.Settings.RippleWaveSpeedCmPerSec, StepSeconds, static_cast<float>(FMath::Min(CellSize.X, CellSize.Y)), GetWaterTier(Scene.Settings), bReachedCap, WaveSpeedScale);
	}

	FTDFluidGpuActorInput MakeOptimizationActor(const FTDWaterGpuOptimizationCase& Case, const FTDWaterGpuChannelScene& Scene, int32 FrameIndex)
	{
		const FVector2D CellSize = ComputeWaterCellSize(Scene.Settings);
		const int32 FieldDivisor = GetWaterFieldDivisor(Scene.Settings);
		const int32 FieldResolution = GetWaterFieldResolution(Scene.Settings);
		FTDFluidGpuActorInput Actor = MakeChannelActor(Scene, FrameIndex);
		Actor.DeltaTimeSeconds = GetOptimizationStepSeconds(FrameIndex);
		if (!Case.bTreatsDryCellsAsSolid)
		{
			for (FTDFluidGpuTerrainCellUpdate& TerrainUpdate : Actor.WaterTerrainUpdates)
			{
				TerrainUpdate.ElevationCm = 3.0f * static_cast<float>(TerrainUpdate.Cell.X) - 50.0f;
			}
		}

		if (Case.bShiftsWindow && FrameIndex >= OptimizationShiftFrameIndex)
		{
			Actor.WindowCenterPlaneCm = FVector2D(FieldDivisor * CellSize.X, 0.0);
			Actor.WaterFieldOrigin = FIntPoint(1 % FieldResolution, 0);
			Actor.ShiftCells = FrameIndex == OptimizationShiftFrameIndex ? FIntPoint(FieldDivisor, 0) : FIntPoint::ZeroValue;
			Actor.bHasObstacleListChanged = Actor.bHasObstacleListChanged || FrameIndex == OptimizationShiftFrameIndex;
		}

		const double Phase = FrameIndex * 0.31;
		FTDFluidGpuStampInput Stamp = MakeModuleWaterStamp(Actor.WindowCenterPlaneCm + FVector2D(500.0 * FMath::Cos(Phase), 200.0 * FMath::Sin(Phase)), 3.0f * static_cast<float>(CellSize.X), 0.6f, 1.5f);
		Stamp.VelocityPlaneCmPerSec = FVector2D(-200.0 * FMath::Sin(Phase), 200.0 * FMath::Cos(Phase));
		Stamp.VelocityStrength = 0.5f;
		Actor.Stamps.Add(Stamp);
		Actor.Stamps.Add(MakeModuleWaterStamp(Actor.WindowCenterPlaneCm + FVector2D(-300.0 * FMath::Cos(Phase), 100.0 * FMath::Sin(Phase)), 4.0f * static_cast<float>(CellSize.X), 0.0f, -2.0f));
		return Actor;
	}

	const TCHAR* GetStateTextureLabel(ETDWaterGpuStateTexture TextureKind)
	{
		switch (TextureKind)
		{
		case ETDWaterGpuStateTexture::Velocity:
			return TEXT("Velocity");
		case ETDWaterGpuStateTexture::VelocityScratch:
			return TEXT("VelocityScratch");
		case ETDWaterGpuStateTexture::Density:
			return TEXT("Density");
		case ETDWaterGpuStateTexture::Ripple:
			return TEXT("Ripple");
		case ETDWaterGpuStateTexture::RippleScratch:
			return TEXT("RippleScratch");
		case ETDWaterGpuStateTexture::Pressure:
			return TEXT("Pressure");
		default:
			return TEXT("Other");
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterModuleRhiStateBytesMatchTableTest,
	"TDGame.FluidFXRhi.Water.Module.StateBytesMatchTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterModuleRhiStateBytesMatchTableTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterModuleRhiTestPrivate;

	if (!CanRunWaterModuleRhiTest(*this, TEXT("Water.Module.StateBytesMatchTable")))
	{
		return true;
	}

	struct FTDWaterStateBytesCase
	{
		const TCHAR* Label = TEXT("");
		int32 Resolution = 0;
		int64 DefaultTableStateBytes = 0;
	};
	const FTDWaterStateBytesCase Cases[] = {
		{ TEXT("192"), 192, 3022848 },
		{ TEXT("1024"), 1024, 47710208 },
	};

	for (const FTDWaterStateBytesCase& Case : Cases)
	{
		FTDWaterGpuChannelScene Scene;
		Scene.Settings = MakeModuleWaterSettings(Case.Resolution, true);
		FTDWaterGpuRunRequest Request;
		Request.FrameCount = 1;
		const FTDWaterGpuRunResult Run = RunWaterGpuFrames(Request, [Scene](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
		{
			OutFrame.Actors.Add(MakeChannelActor(Scene, FrameIndex));
		});
		if (!TestTrue(*FString::Printf(TEXT("%s water actor stepped in a water batch"), Case.Label), Run.bHasWaterBatch))
		{
			continue;
		}

		const FTDFluidGpuBatchKey& Key = Run.WaterKey;
		const int32 FieldResolution = Key.ResolutionTier / FTDFluidGpuBatch::ComputeWaterFieldDivisor(Key.ResolutionTier);
		const int32 PressureResolution = Key.WaterPressureMode == ETDFluidGpuWaterPressureMode::JacobiOpenBorders ? Key.ResolutionTier : Key.ResolutionTier / 2;
		const int64 ExpectedStateBytes = ComputeExpectedWaterStateBytes(Key.ResolutionTier, FieldResolution, PressureResolution, Key.bUsesFloat32WaterVelocity, Key.bUsesFloat32Ripple);
		const bool bUsesDefaultFormats = Key.bUsesFloat32WaterVelocity
			&& Key.bUsesFloat32Ripple == (Key.ResolutionTier <= TDFluidGpuSolverConstants::WaterExactTierMaximumResolution);
		const FString StateLine = FString::Printf(TEXT("TDFluidGpuWaterStateBytes: case=%s tier=%d field=%d pressure=%d velocity32=%d ripple32=%d slice_capacity=%d measured=%lld expected=%lld table=%lld default_formats=%d"),
			Case.Label,
			Key.ResolutionTier,
			FieldResolution,
			PressureResolution,
			Key.bUsesFloat32WaterVelocity ? 1 : 0,
			Key.bUsesFloat32Ripple ? 1 : 0,
			Run.WaterSliceCapacity,
			Run.WaterStateBytes,
			ExpectedStateBytes,
			Case.DefaultTableStateBytes,
			bUsesDefaultFormats ? 1 : 0);
		UE_LOG(LogTDFluidGpuWaterModuleRhiTests, Display, TEXT("%s"), *StateLine);
		AddInfo(StateLine);

		TestEqual(*FString::Printf(TEXT("%s water batch tier"), Case.Label), Key.ResolutionTier, Case.Resolution);
		TestEqual(*FString::Printf(TEXT("%s water batch keeps an exact one slice capacity"), Case.Label), Run.WaterSliceCapacity, 1);
		TestEqual(*FString::Printf(TEXT("%s ComputeTDFluidGpuBatchStateBytes equals the 5.6 linear StateBytes"), Case.Label), Run.WaterStateBytes, ExpectedStateBytes);
		TestEqual(*FString::Printf(TEXT("%s EstimateWaterActorMemory StateBytes equals the 5.6 linear StateBytes"), Case.Label), FTDFluidGpuBatch::EstimateWaterActorMemory(Key).StateBytes, ExpectedStateBytes);
		if (bUsesDefaultFormats)
		{
			TestEqual(*FString::Printf(TEXT("%s default format StateBytes equals the 5.6 table row"), Case.Label), ExpectedStateBytes, Case.DefaultTableStateBytes);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterModuleRhiShiftMovesWaterStateExactlyTest,
	"TDGame.FluidFXRhi.Water.Module.ShiftMovesWaterStateExactly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterModuleRhiShiftMovesWaterStateExactlyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterModuleRhiTestPrivate;

	if (!CanRunWaterModuleRhiTest(*this, TEXT("Water.Module.ShiftMovesWaterStateExactly")))
	{
		return true;
	}

	struct FTDWaterShiftCase
	{
		const TCHAR* Label = TEXT("");
		int32 Resolution = 0;
		FIntPoint ShiftCells = FIntPoint::ZeroValue;
	};
	const FTDWaterShiftCase Cases[] = {
		{ TEXT("192"), 192, FIntPoint(1, -2) },
		{ TEXT("1024"), 1024, FIntPoint(4, -8) },
	};

	for (const FTDWaterShiftCase& Case : Cases)
	{
		FTDWaterGpuShearScene Scene;
		Scene.Settings = MakeShearSettings(Case.Resolution);
		Scene.ShiftCells = Case.ShiftCells;
		const int32 FieldDivisor = GetWaterFieldDivisor(Scene.Settings);
		const int32 FieldResolution = GetWaterFieldResolution(Scene.Settings);
		const FIntPoint FieldShift = ComputeFieldShift(Scene);
		TestTrue(*FString::Printf(TEXT("%s shift is a whole number of field cells"), Case.Label), Case.ShiftCells.X % FieldDivisor == 0 && Case.ShiftCells.Y % FieldDivisor == 0);

		FTDWaterGpuRunRequest Request;
		Request.FrameCount = 2;
		Request.SnapshotFrameCounts = { 1, 2 };
		Request.SnapshotTextures = { ETDWaterGpuStateTexture::Velocity, ETDWaterGpuStateTexture::Density, ETDWaterGpuStateTexture::Ripple, ETDWaterGpuStateTexture::Pressure, ETDWaterGpuStateTexture::GuideField, ETDWaterGpuStateTexture::SurfaceHeightField, ETDWaterGpuStateTexture::TerrainField };
		const FTDWaterGpuRunResult Run = RunWaterGpuFrames(Request, [Scene](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
		{
			OutFrame.Actors.Add(MakeShearActor(Scene, FrameIndex));
		});
		if (!TestTrue(*FString::Printf(TEXT("%s read both state snapshots"), Case.Label), Run.bHasWaterBatch && Run.bHasReadAllSnapshots && Run.Snapshots.Num() == 2))
		{
			continue;
		}

		const FTDWaterGpuStateSnapshot& Before = Run.Snapshots[0];
		const FTDWaterGpuStateSnapshot& After = Run.Snapshots[1];
		const FTDWaterGpuTextureSlice& VelocityBefore = GetSnapshotTexture(Before, ETDWaterGpuStateTexture::Velocity);
		const FTDWaterGpuTextureSlice& DensityBefore = GetSnapshotTexture(Before, ETDWaterGpuStateTexture::Density);
		const FTDWaterGpuTextureSlice& RippleBefore = GetSnapshotTexture(Before, ETDWaterGpuStateTexture::Ripple);
		TestTrue(*FString::Printf(TEXT("%s state before the shift holds stamped density"), Case.Label), FindSliceMaximumMagnitude(DensityBefore, 0) > 0.0);
		TestTrue(*FString::Printf(TEXT("%s state before the shift holds stamped ripple height"), Case.Label), FindSliceMaximumMagnitude(RippleBefore, 0) > 0.0);
		TestTrue(*FString::Printf(TEXT("%s state before the shift holds guided shear velocity"), Case.Label), FindSliceMaximumMagnitude(VelocityBefore, 0) > 0.0);

		const auto IsIncomingVelocityExpected = [&After, FieldDivisor, FieldResolution, FieldShift](const FIntPoint& Cell)
		{
			const FTDWaterGpuTextureSlice& VelocityAfter = GetSnapshotTexture(After, ETDWaterGpuStateTexture::Velocity);
			const float ExpectedSpeed = ComputeShearFlowSpeed(Cell.Y / FieldDivisor + FieldShift.Y, FieldResolution);
			return ReadSliceChannel(VelocityAfter, Cell.X, Cell.Y, 0) == ExpectedSpeed && ReadSliceChannel(VelocityAfter, Cell.X, Cell.Y, 1) == 0.0f;
		};
		const auto IsIncomingDensityExpected = [&After](const FIntPoint& Cell)
		{
			return IsSliceTexelZero(GetSnapshotTexture(After, ETDWaterGpuStateTexture::Density), Cell, 2);
		};
		const auto IsIncomingRippleExpected = [&After](const FIntPoint& Cell)
		{
			return IsSliceTexelZero(GetSnapshotTexture(After, ETDWaterGpuStateTexture::Ripple), Cell, 2);
		};
		const auto IsIncomingPressureExpected = [&After](const FIntPoint& Cell)
		{
			return IsSliceTexelZero(GetSnapshotTexture(After, ETDWaterGpuStateTexture::Pressure), Cell, 1);
		};

		const FTDWaterGpuShiftComparison VelocityComparison = CompareShiftedSlices(VelocityBefore, GetSnapshotTexture(After, ETDWaterGpuStateTexture::Velocity), Case.ShiftCells, IsIncomingVelocityExpected);
		const FTDWaterGpuShiftComparison DensityComparison = CompareShiftedSlices(DensityBefore, GetSnapshotTexture(After, ETDWaterGpuStateTexture::Density), Case.ShiftCells, IsIncomingDensityExpected);
		const FTDWaterGpuShiftComparison RippleComparison = CompareShiftedSlices(RippleBefore, GetSnapshotTexture(After, ETDWaterGpuStateTexture::Ripple), Case.ShiftCells, IsIncomingRippleExpected);
		const int32 PressureDivisor = FMath::Max(After.WaterParameters.PressureDivisor, 1);
		const FIntPoint PressureShift(Case.ShiftCells.X / PressureDivisor, Case.ShiftCells.Y / PressureDivisor);
		const FTDWaterGpuShiftComparison PressureComparison = CompareShiftedSlices(GetSnapshotTexture(Before, ETDWaterGpuStateTexture::Pressure), GetSnapshotTexture(After, ETDWaterGpuStateTexture::Pressure), PressureShift, IsIncomingPressureExpected);
		const FTDWaterGpuFieldComparison FieldComparison = CompareShiftedFields(Before, After, FieldShift, FieldResolution);
		const FIntPoint ExpectedFieldOrigin(WrapFieldCoordinate(FieldShift.X, FieldResolution), WrapFieldCoordinate(FieldShift.Y, FieldResolution));

		const FString ShiftLine = FString::Printf(TEXT("TDFluidGpuWaterShift: case=%s shift=(%d,%d) field_shift=(%d,%d) pressure_shift=(%d,%d) velocity_kept=%d/%d velocity_incoming_mismatch=%d density_kept_mismatch=%d density_incoming_mismatch=%d ripple_kept_mismatch=%d ripple_incoming_mismatch=%d pressure_kept_mismatch=%d field_kept_mismatch=%d field_band_mismatch=%d/%d field_origin_after=(%d,%d)"),
			Case.Label,
			Case.ShiftCells.X,
			Case.ShiftCells.Y,
			FieldShift.X,
			FieldShift.Y,
			PressureShift.X,
			PressureShift.Y,
			VelocityComparison.KeptMismatchCount,
			VelocityComparison.KeptCellCount,
			VelocityComparison.IncomingMismatchCount,
			DensityComparison.KeptMismatchCount,
			DensityComparison.IncomingMismatchCount,
			RippleComparison.KeptMismatchCount,
			RippleComparison.IncomingMismatchCount,
			PressureComparison.KeptMismatchCount,
			FieldComparison.KeptMismatchCount,
			FieldComparison.BandMismatchCount,
			FieldComparison.BandCellCount,
			After.WaterParameters.FieldOrigin.X,
			After.WaterParameters.FieldOrigin.Y);
		UE_LOG(LogTDFluidGpuWaterModuleRhiTests, Display, TEXT("%s"), *ShiftLine);
		AddInfo(ShiftLine);

		TestTrue(*FString::Printf(TEXT("%s velocity kept cells compared"), Case.Label), VelocityComparison.KeptCellCount > 0);
		TestEqual(*FString::Printf(TEXT("%s velocity moves by exactly the shift"), Case.Label), VelocityComparison.KeptMismatchCount, 0);
		TestEqual(*FString::Printf(TEXT("%s incoming velocity is the guide seed"), Case.Label), VelocityComparison.IncomingMismatchCount, 0);
		TestEqual(*FString::Printf(TEXT("%s density moves by exactly the shift"), Case.Label), DensityComparison.KeptMismatchCount, 0);
		TestEqual(*FString::Printf(TEXT("%s incoming density is zero"), Case.Label), DensityComparison.IncomingMismatchCount, 0);
		TestEqual(*FString::Printf(TEXT("%s ripple moves by exactly the shift"), Case.Label), RippleComparison.KeptMismatchCount, 0);
		TestEqual(*FString::Printf(TEXT("%s incoming ripple is zero"), Case.Label), RippleComparison.IncomingMismatchCount, 0);
		TestEqual(*FString::Printf(TEXT("%s pressure moves by exactly the shift divided by the pressure divisor"), Case.Label), PressureComparison.KeptMismatchCount, 0);
		TestEqual(*FString::Printf(TEXT("%s incoming pressure is zero"), Case.Label), PressureComparison.IncomingMismatchCount, 0);
		TestEqual(*FString::Printf(TEXT("%s kept field texels keep their bytes through the toroidal origin"), Case.Label), FieldComparison.KeptMismatchCount, 0);
		TestEqual(*FString::Printf(TEXT("%s field band holds this frame's guide updates"), Case.Label), FieldComparison.BandMismatchCount, 0);
		TestEqual(*FString::Printf(TEXT("%s field origin X advanced by the field shift"), Case.Label), After.WaterParameters.FieldOrigin.X, ExpectedFieldOrigin.X);
		TestEqual(*FString::Printf(TEXT("%s field origin Y advanced by the field shift"), Case.Label), After.WaterParameters.FieldOrigin.Y, ExpectedFieldOrigin.Y);
	}

	FTDWaterGpuShearScene GatherScene;
	GatherScene.Settings = MakeModuleWaterSettings(1024, true);
	GatherScene.ShiftCells = FIntPoint(4, -8);
	FTDWaterGpuRunRequest GatherRequest;
	GatherRequest.FrameCount = PressureGatherShiftFrameIndex + 1;
	GatherRequest.SnapshotFrameCounts = { PressureGatherShiftFrameIndex, PressureGatherShiftFrameIndex + 1 };
	GatherRequest.SnapshotTextures = { ETDWaterGpuStateTexture::Pressure, ETDWaterGpuStateTexture::PressureScratch };
	const FTDWaterGpuRunResult GatherRun = RunWaterGpuFrames(GatherRequest, [GatherScene](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
	{
		OutFrame.Actors.Add(MakePressureGatherActor(GatherScene, FrameIndex));
	});
	if (!TestTrue(TEXT("1024 pressure gather run read both snapshots"), GatherRun.bHasWaterBatch && GatherRun.bHasReadAllSnapshots && GatherRun.Snapshots.Num() == 2))
	{
		return false;
	}

	const FTDWaterGpuTextureSlice& HalfPressureBefore = GetSnapshotTexture(GatherRun.Snapshots[0], ETDWaterGpuStateTexture::Pressure);
	const FTDWaterGpuTextureSlice& GatheredPressure = GetSnapshotTexture(GatherRun.Snapshots[1], ETDWaterGpuStateTexture::PressureScratch);
	const double HalfPressureMagnitude = FindSliceMaximumMagnitude(HalfPressureBefore, 0);
	const int32 GatherDifferenceCount = CountSliceByteDifferences(HalfPressureBefore, GatheredPressure);
	const FString GatherLine = FString::Printf(TEXT("TDFluidGpuWaterShiftPressureGather: tier=1024 mode=%d half_pressure_resolution=%d max_abs_half_pressure=%.6e gathered_difference_count=%d"),
		static_cast<int32>(GatherRun.WaterKey.WaterPressureMode),
		HalfPressureBefore.Resolution,
		HalfPressureMagnitude,
		GatherDifferenceCount);
	UE_LOG(LogTDFluidGpuWaterModuleRhiTests, Display, TEXT("%s"), *GatherLine);
	AddInfo(GatherLine);
	TestTrue(TEXT("1024 water batch uses the half resolution pressure"), GatherRun.WaterKey.WaterPressureMode != ETDFluidGpuWaterPressureMode::JacobiOpenBorders);
	TestEqual(TEXT("1024 half pressure state is half resolution"), HalfPressureBefore.Resolution, 512);
	TestTrue(TEXT("1024 velocity jet leaves nonzero half pressure before the shift"), HalfPressureMagnitude > 0.0);
	TestEqual(TEXT("1024 shift gather copies every half pressure texel bit for bit"), GatherDifferenceCount, 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterModuleRhiMultigridOpenBoundaryConvergesTest,
	"TDGame.FluidFXRhi.Water.Module.MultigridOpenBoundaryConverges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterModuleRhiMultigridOpenBoundaryConvergesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterModuleRhiTestPrivate;

	if (!CanRunWaterModuleRhiTest(*this, TEXT("Water.Module.MultigridOpenBoundaryConverges")))
	{
		return true;
	}

	constexpr int32 WarmupFrameCount = 30;
	constexpr double CycleOneDivergenceRatioLimit = 0.2;
	constexpr double CycleFourDivergenceRatioLimit = 1.0e-2;
	constexpr double FluxImbalanceMarginOverReference = 0.005;
	constexpr double ReferenceFluxImbalanceSanityLimit = 0.04;
	constexpr int32 BoundaryBandWidthCells = 4;
	constexpr int32 InteriorSolidMarginCells = 2;
	constexpr double BandRelativeErrorLimit = 0.10;
	constexpr double InteriorRelativeErrorLimit = 0.05;
	constexpr int32 ReferenceMaximumIterationCount = 40000;
	constexpr double ReferenceRelativeResidual = 1.0e-10;
	constexpr int32 MeanDriftFrameCount = 60;
	constexpr int32 MeanDriftFirstFrame = 10;
	constexpr double MeanDriftRangeFraction = 0.05;
	constexpr int32 DecayStepFrameCount = 2;
	constexpr int32 DecayFramesEnabled = 32;
	constexpr int32 DecayFramesDisabled = 0;
	const TCHAR* const CyclesVariableName = TEXT("TD.FluidFX.GpuPressureCycles");
	const TCHAR* const DecayVariableName = TEXT("TD.FluidFX.GpuPressureWarmStartDecayFrames");
	const TArray<int32> BoundaryProbeCycles = { 1, 4, 16 };

	const bool BorderModes[] = { true, false };
	const int32 CycleCounts[] = { 1, 4 };
	for (const bool bOpensWaterBorders : BorderModes)
	{
		for (const int32 CycleCount : CycleCounts)
		{
			const FString CaseLabel = FString::Printf(TEXT("%s cycles %d"), bOpensWaterBorders ? TEXT("open") : TEXT("closed"), CycleCount);
			FTDWaterGpuConsoleVariableScope CycleScope(CyclesVariableName, CycleCount);
			if (!TestTrue(*FString::Printf(TEXT("%s pressure cycle console variable applied"), *CaseLabel), CycleScope.IsValid()))
			{
				continue;
			}

			const FTDWaterGpuChannelScene Scene = MakeRockChannelScene(bOpensWaterBorders);
			const bool bRunsBoundaryProbe = bOpensWaterBorders && CycleCount == 1;
			FTDWaterGpuRunRequest Request;
			Request.FrameCount = WarmupFrameCount;
			Request.SnapshotFrameCounts = { WarmupFrameCount };
			Request.SnapshotTextures = { ETDWaterGpuStateTexture::Velocity, ETDWaterGpuStateTexture::Pressure };
			Request.ProbeCycleCounts = bRunsBoundaryProbe ? BoundaryProbeCycles : TArray<int32>{ 1 };
			const FTDWaterGpuRunResult Run = RunWaterGpuFrames(Request, [Scene](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
			{
				OutFrame.Actors.Add(MakeChannelActor(Scene, FrameIndex));
			});
			const bool bHasCaptures = Run.bHasWaterBatch && Run.bHasReadAllSnapshots && Run.Snapshots.Num() == 1 && Run.Probe.bIsValid;
			if (!TestTrue(*FString::Printf(TEXT("%s read the water pressure probe and the state snapshot"), *CaseLabel), bHasCaptures))
			{
				continue;
			}

			TestTrue(*FString::Printf(TEXT("%s runs the half resolution multigrid"), *CaseLabel), Run.WaterKey.WaterPressureMode == ETDFluidGpuWaterPressureMode::HalfResolutionMultigrid);
			TestEqual(*FString::Printf(TEXT("%s batch key carries the cycle count"), *CaseLabel), Run.WaterKey.MultigridCycles, CycleCount);
			TestTrue(*FString::Printf(TEXT("%s open border flag matches the setting"), *CaseLabel), (Run.Probe.OpenBorderFlag != 0) == bOpensWaterBorders);

			const FTDWaterGpuFineSystem System = BuildFineSystem(Run, Run.Probe.Velocity);
			const FTDWaterGpuHalfResidual HalfResidual = ComputeHalfResidual(System, GetSnapshotTexture(Run.Snapshots[0], ETDWaterGpuStateTexture::Pressure));
			const double DivergenceRatio = HalfResidual.ResidualNorm / FMath::Max(HalfResidual.RhsNorm, UE_DOUBLE_SMALL_NUMBER);
			const double DivergenceRatioLimit = CycleCount == 1 ? CycleOneDivergenceRatioLimit : CycleFourDivergenceRatioLimit;
			const FTDWaterGpuFlux Flux = MeasureChannelFlux(System, GetSnapshotTexture(Run.Snapshots[0], ETDWaterGpuStateTexture::Velocity));
			const double FluxImbalance = FMath::Abs(Flux.Inflow - Flux.Outflow) / FMath::Max(FMath::Abs(Flux.Inflow), UE_DOUBLE_SMALL_NUMBER);
			const double IncompatibleFraction = HalfResidual.IncompatibleNorm / FMath::Max(FMath::Sqrt(FMath::Square(HalfResidual.RhsNorm) + FMath::Square(HalfResidual.IncompatibleNorm)), UE_DOUBLE_SMALL_NUMBER);
			const FString ConvergenceLine = FString::Printf(TEXT("TDFluidGpuWaterMultigridConvergence: case=%s active_half_cells=%d anchored_components=%d closed_components=%d half_rhs_l2=%.6e half_residual_l2=%.6e ratio=%.6e limit=%.1e inflow=%.4f outflow=%.4f flux_imbalance=%.6f incompatible_fraction=%.6e"),
				*CaseLabel,
				HalfResidual.ActiveCellCount,
				HalfResidual.AnchoredComponentCount,
				HalfResidual.ClosedComponentCount,
				HalfResidual.RhsNorm,
				HalfResidual.ResidualNorm,
				DivergenceRatio,
				DivergenceRatioLimit,
				Flux.Inflow,
				Flux.Outflow,
				FluxImbalance,
				IncompatibleFraction);
			UE_LOG(LogTDFluidGpuWaterModuleRhiTests, Display, TEXT("%s"), *ConvergenceLine);
			AddInfo(ConvergenceLine);

			TestTrue(*FString::Printf(TEXT("%s rebuilt the fine pressure system from the probe"), *CaseLabel), System.bIsValid && HalfResidual.bIsValid);
			TestTrue(*FString::Printf(TEXT("%s half divergence after one projection is at most %.0e of the divergence before it"), *CaseLabel, DivergenceRatioLimit), DivergenceRatio <= DivergenceRatioLimit);
			if (!bOpensWaterBorders)
			{
				continue;
			}

			TestTrue(*FString::Printf(TEXT("%s channel carries inflow"), *CaseLabel), Flux.Inflow > 0.0);
			const FTDWaterGpuReferenceSolve Reference = SolveFineReferencePressure(System, ReferenceMaximumIterationCount, ReferenceRelativeResidual);
			if (!Reference.bHasConverged)
			{
				AddError(FString::Printf(TEXT("%s: harness defect, the CPU double CG reference stopped at relative residual %.3e after %d iterations"), *CaseLabel, Reference.RelativeResidual, Reference.IterationCount));
				continue;
			}

			const double ReferenceFluxImbalance = MeasureFloorFluxImbalance(System, Run.Probe.CompositeSdf, Reference.Pressure, Run.WaterActorParameters.ObstacleBoundaryBandCm, true);
			const double FluxImbalanceLimit = ReferenceFluxImbalance + FluxImbalanceMarginOverReference;
			const FString FluxCriterionLine = FString::Printf(TEXT("TDFluidGpuWaterFluxCriterion: case=%s gpu_imbalance=%.6f reference_imbalance=%.6f limit=%.6f"),
				*CaseLabel,
				FluxImbalance,
				ReferenceFluxImbalance,
				FluxImbalanceLimit);
			UE_LOG(LogTDFluidGpuWaterModuleRhiTests, Display, TEXT("%s"), *FluxCriterionLine);
			AddInfo(FluxCriterionLine);
			TestTrue(*FString::Printf(TEXT("%s full resolution exact solve flux imbalance %.4f is measured and at most 4%%"), *CaseLabel, ReferenceFluxImbalance), ReferenceFluxImbalance >= 0.0 && ReferenceFluxImbalance <= ReferenceFluxImbalanceSanityLimit);
			TestTrue(*FString::Printf(TEXT("%s inflow outflow imbalance %.4f is at most the exact solve imbalance plus 0.5 percentage points %.4f"), *CaseLabel, FluxImbalance, FluxImbalanceLimit), FluxImbalance <= FluxImbalanceLimit);

			if (!bRunsBoundaryProbe)
			{
				continue;
			}

			const int32 CycleSixteenIndex = Run.Probe.RecordedCycleCounts.Find(BoundaryProbeCycles.Last());
			const FTDWaterGpuTextureSlice* CycleSixteenPressure = Run.Probe.PressureFullByCycle.IsValidIndex(CycleSixteenIndex) ? &Run.Probe.PressureFullByCycle[CycleSixteenIndex] : nullptr;
			const FString FluxFloorLine = MakeFluxFloorLine(System, Reference, Run.Probe.CompositeSdf, CycleSixteenPressure, FluxImbalance, Run.WaterActorParameters.ObstacleBoundaryBandCm);
			UE_LOG(LogTDFluidGpuWaterModuleRhiTests, Display, TEXT("%s"), *FluxFloorLine);
			AddInfo(FluxFloorLine);

			TestTrue(TEXT("Boundary probe recorded cycles 1, 4 and 16"), Run.Probe.RecordedCycleCounts == BoundaryProbeCycles);
			for (int32 RecordIndex = 0; RecordIndex < Run.Probe.RecordedCycleCounts.Num() && RecordIndex < Run.Probe.PressureFullByCycle.Num(); ++RecordIndex)
			{
				const int32 RecordedCycleCount = Run.Probe.RecordedCycleCounts[RecordIndex];
				const FTDWaterGpuBoundaryError BoundaryError = MeasureBoundaryError(System, Reference, Run.Probe.PressureFullByCycle[RecordIndex], BoundaryBandWidthCells, InteriorSolidMarginCells);
				const FString BoundaryLine = FString::Printf(TEXT("TDFluidGpuWaterBoundaryPosition: cycles=%d reference_iterations=%d reference_relative_residual=%.3e band_cells=%d band_relative_l2=%.6f interior_cells=%d interior_relative_l2=%.6f"),
					RecordedCycleCount,
					Reference.IterationCount,
					Reference.RelativeResidual,
					BoundaryError.BandCellCount,
					BoundaryError.BandRelativeError,
					BoundaryError.InteriorCellCount,
					BoundaryError.InteriorRelativeError);
				UE_LOG(LogTDFluidGpuWaterModuleRhiTests, Display, TEXT("%s"), *BoundaryLine);
				AddInfo(BoundaryLine);
				if (RecordedCycleCount != BoundaryProbeCycles.Last())
				{
					continue;
				}

				TestTrue(TEXT("Boundary band and interior both hold fluid cells"), BoundaryError.BandCellCount > 0 && BoundaryError.InteriorCellCount > 0);
				TestTrue(TEXT("Cycle 16 open edge band relative L2 error is at most 10%"), BoundaryError.BandRelativeError <= BandRelativeErrorLimit);
				TestTrue(TEXT("Cycle 16 interior relative L2 error is at most 5%"), BoundaryError.InteriorRelativeError <= InteriorRelativeErrorLimit);
			}
		}
	}

	{
		FTDWaterGpuConsoleVariableScope CycleScope(CyclesVariableName, 1);
		const FTDWaterGpuChannelScene PillarScene = MakePillarChannelScene();
		FTDWaterGpuRunRequest DriftRequest;
		DriftRequest.FrameCount = MeanDriftFrameCount;
		DriftRequest.SnapshotTextures = { ETDWaterGpuStateTexture::Pressure };
		for (int32 FrameCount = 1; FrameCount <= MeanDriftFrameCount; ++FrameCount)
		{
			DriftRequest.SnapshotFrameCounts.Add(FrameCount);
		}

		const FTDWaterGpuRunResult DriftRun = RunWaterGpuFrames(DriftRequest, [PillarScene](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
		{
			OutFrame.Actors.Add(MakeChannelActor(PillarScene, FrameIndex));
		});
		if (TestTrue(TEXT("Closed pillar channel read every half pressure snapshot"), CycleScope.IsValid() && DriftRun.bHasReadAllSnapshots && DriftRun.Snapshots.Num() == MeanDriftFrameCount))
		{
			FString DeltaSeries;
			TArray<double> Deltas;
			for (const FTDWaterGpuStateSnapshot& Snapshot : DriftRun.Snapshots)
			{
				const FTDWaterGpuTextureSlice& HalfPressure = GetSnapshotTexture(Snapshot, ETDWaterGpuStateTexture::Pressure);
				const double Delta = ComputeHalfPressureGroupMean(HalfPressure, true) - ComputeHalfPressureGroupMean(HalfPressure, false);
				Deltas.Add(Delta);
				DeltaSeries += FString::Printf(TEXT("%s%d:%.4e"), DeltaSeries.IsEmpty() ? TEXT("") : TEXT(","), Snapshot.FrameCount, Delta);
			}

			const double FinalRange = ComputeSliceRange(GetSnapshotTexture(DriftRun.Snapshots.Last(), ETDWaterGpuStateTexture::Pressure));
			const double DeltaDrift = FMath::Abs(Deltas[MeanDriftFrameCount - 1] - Deltas[MeanDriftFirstFrame - 1]);
			const FString DriftLine = FString::Printf(TEXT("TDFluidGpuWaterClosedMeanWriteBack: frames=%d delta_drift_10_to_60=%.6e half_pressure_range_60=%.6e limit=%.6e delta=[%s]"),
				MeanDriftFrameCount,
				DeltaDrift,
				FinalRange,
				MeanDriftRangeFraction * FinalRange,
				*DeltaSeries);
			UE_LOG(LogTDFluidGpuWaterModuleRhiTests, Display, TEXT("%s"), *DriftLine);
			AddInfo(DriftLine);
			TestTrue(TEXT("Closed half pressure has a nonzero range at frame 60"), FinalRange > 0.0);
			TestTrue(TEXT("Closed half mean write back keeps group A minus group C offset from drifting by more than 5% of the range"), DeltaDrift <= MeanDriftRangeFraction * FinalRange);
		}
	}

	for (const bool bOpensWaterBorders : BorderModes)
	{
		const TCHAR* DecayLabel = bOpensWaterBorders ? TEXT("open") : TEXT("closed");
		FTDWaterGpuConsoleVariableScope CycleScope(CyclesVariableName, 1);
		TArray<FTDWaterGpuTextureSlice> DecayPressures;
		for (const int32 DecayFrames : { DecayFramesEnabled, DecayFramesDisabled })
		{
			FTDWaterGpuConsoleVariableScope DecayScope(DecayVariableName, DecayFrames);
			const FTDWaterGpuChannelScene Scene = MakeRockChannelScene(bOpensWaterBorders);
			FTDWaterGpuRunRequest DecayRequest;
			DecayRequest.FrameCount = DecayStepFrameCount;
			DecayRequest.SnapshotFrameCounts = { DecayStepFrameCount };
			DecayRequest.SnapshotTextures = { ETDWaterGpuStateTexture::Pressure };
			const FTDWaterGpuRunResult DecayRun = RunWaterGpuFrames(DecayRequest, [Scene](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
			{
				OutFrame.Actors.Add(MakeChannelActor(Scene, FrameIndex));
			});
			const bool bHasDecaySnapshot = CycleScope.IsValid() && DecayScope.IsValid() && DecayRun.bHasReadAllSnapshots && DecayRun.Snapshots.Num() == 1;
			if (TestTrue(*FString::Printf(TEXT("%s decay %d run read the half pressure"), DecayLabel, DecayFrames), bHasDecaySnapshot))
			{
				DecayPressures.Add(GetSnapshotTexture(DecayRun.Snapshots[0], ETDWaterGpuStateTexture::Pressure));
			}
		}

		if (DecayPressures.Num() != 2)
		{
			continue;
		}

		const int32 DecayDifferenceCount = CountSliceByteDifferences(DecayPressures[0], DecayPressures[1]);
		const FString DecayLine = FString::Printf(TEXT("TDFluidGpuWaterWarmStartDecay: borders=%s decay_32_vs_0_difference_count=%d max_abs_half_pressure=%.6e"), DecayLabel, DecayDifferenceCount, FindSliceMaximumMagnitude(DecayPressures[0], 0));
		UE_LOG(LogTDFluidGpuWaterModuleRhiTests, Display, TEXT("%s"), *DecayLine);
		AddInfo(DecayLine);
		if (bOpensWaterBorders)
		{
			TestEqual(TEXT("Open entry half pressure ignores the warm start decay bit for bit"), DecayDifferenceCount, 0);
			continue;
		}

		TestTrue(TEXT("Closed entry half pressure follows the warm start decay"), DecayDifferenceCount > 0);
	}

	AddInfo(TEXT("TDFluidGpuWaterMultigridConvergence: S8 (e) is covered by the FX-4a multigrid tests TDGame.FluidFXRhi.Multigrid* run in the same RHI suite"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterModuleRhiOptimizationsMatchLegacyTest,
	"TDGame.FluidFXRhi.Water.Module.OptimizationsMatchLegacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterModuleRhiOptimizationsMatchLegacyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterModuleRhiTestPrivate;

	if (!CanRunWaterModuleRhiTest(*this, TEXT("Water.Module.OptimizationsMatchLegacy")))
	{
		return true;
	}

	const FTDWaterGpuOptimizationCase Cases[] = {
		{ TEXT("192 open fast ripple moving"), 192, true, true, 1200.0f, true },
		{ TEXT("192 closed slow ripple"), 192, false, true, 120.0f, false },
		{ TEXT("192 open dry cells fluid slope gravity"), 192, true, false, 120.0f, false },
		{ TEXT("512 open slow ripple moving"), 512, true, true, 120.0f, true },
		{ TEXT("512 closed fast ripple"), 512, false, true, 1200.0f, false },
		{ TEXT("1024 open slow ripple moving"), 1024, true, true, 120.0f, true },
		{ TEXT("1024 closed slow ripple"), 1024, false, true, 120.0f, false },
		{ TEXT("192 open iterations 13 moving"), 192, true, true, 120.0f, true, 13 },
		{ TEXT("192 closed iterations 5"), 192, false, true, 120.0f, false, 5 },
		{ TEXT("192 open dry cells iterations 16"), 192, true, false, 120.0f, false, 16 },
	};
	const TArray<ETDWaterGpuStateTexture> ComparedTextures = { ETDWaterGpuStateTexture::Velocity, ETDWaterGpuStateTexture::VelocityScratch, ETDWaterGpuStateTexture::Density, ETDWaterGpuStateTexture::Ripple, ETDWaterGpuStateTexture::RippleScratch, ETDWaterGpuStateTexture::Pressure };

	bool bSawOddRippleSubstepCount = false;
	bool bSawEvenRippleSubstepCount = false;
	for (const FTDWaterGpuOptimizationCase& Case : Cases)
	{
		const FTDWaterGpuChannelScene Scene = MakeOptimizationScene(Case);
		FString SubstepSeries;
		for (int32 FrameIndex = 0; FrameIndex < OptimizationFrameCount; ++FrameIndex)
		{
			const int32 SubstepCount = ComputeOptimizationRippleSubstepCount(Scene, FrameIndex);
			bSawOddRippleSubstepCount |= SubstepCount % 2 == 1;
			bSawEvenRippleSubstepCount |= SubstepCount > 0 && SubstepCount % 2 == 0;
			if (FrameIndex < 4)
			{
				SubstepSeries += FString::Printf(TEXT("%s%d"), SubstepSeries.IsEmpty() ? TEXT("") : TEXT(","), SubstepCount);
			}
		}

		FTDWaterGpuRunRequest Request;
		Request.FrameCount = OptimizationFrameCount;
		Request.SnapshotFrameCounts = { 1, OptimizationShiftFrameIndex + 1, OptimizationShiftFrameIndex + 2, OptimizationFrameCount };
		Request.SnapshotTextures = ComparedTextures;
		FTDWaterGpuRunResult Runs[2];
		bool bRanBothModes = true;
		for (int32 ModeIndex = 0; ModeIndex < 2 && bRanBothModes; ++ModeIndex)
		{
			const FTDWaterGpuOptimizationVariableScopes Scopes = ApplyOptimizationVariables(ModeIndex);
			if (!TestTrue(*FString::Printf(TEXT("%s mode %d optimization console variables are registered"), Case.Label, ModeIndex), AreOptimizationVariablesApplied(Scopes)))
			{
				bRanBothModes = false;
				break;
			}

			Runs[ModeIndex] = RunWaterGpuFrames(Request, [Case, Scene](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
			{
				OutFrame.Actors.Add(MakeOptimizationActor(Case, Scene, FrameIndex));
			});
			bRanBothModes = TestTrue(*FString::Printf(TEXT("%s mode %d read every state snapshot"), Case.Label, ModeIndex), Runs[ModeIndex].bHasWaterBatch && Runs[ModeIndex].bHasReadAllSnapshots && Runs[ModeIndex].Snapshots.Num() == Request.SnapshotFrameCounts.Num());
		}
		if (!bRanBothModes)
		{
			continue;
		}

		const FTDWaterGpuStateSnapshot& FinalSnapshot = Runs[1].Snapshots.Last();
		TestTrue(*FString::Printf(TEXT("%s final velocity is nonzero"), Case.Label), FindSliceMaximumMagnitude(GetSnapshotTexture(FinalSnapshot, ETDWaterGpuStateTexture::Velocity), 0) > 0.0);
		TestTrue(*FString::Printf(TEXT("%s final ripple height is nonzero"), Case.Label), FindSliceMaximumMagnitude(GetSnapshotTexture(FinalSnapshot, ETDWaterGpuStateTexture::Ripple), 0) > 0.0);

		int32 TotalDifferenceCount = 0;
		for (int32 SnapshotIndex = 0; SnapshotIndex < Runs[0].Snapshots.Num(); ++SnapshotIndex)
		{
			for (const ETDWaterGpuStateTexture TextureKind : ComparedTextures)
			{
				const int32 DifferenceCount = CountSliceByteDifferences(GetSnapshotTexture(Runs[0].Snapshots[SnapshotIndex], TextureKind), GetSnapshotTexture(Runs[1].Snapshots[SnapshotIndex], TextureKind));
				TotalDifferenceCount += DifferenceCount;
				if (!TestEqual(*FString::Printf(TEXT("%s: optimized water state differs from legacy (%s, frame %d, texture %s)"), Case.Label, Case.Label, Runs[0].Snapshots[SnapshotIndex].FrameCount, GetStateTextureLabel(TextureKind)), DifferenceCount, 0))
				{
					break;
				}
			}
		}

		const FString CaseLine = FString::Printf(TEXT("TDFluidGpuWaterOptimizations: case=%s tier=%d borders=%d dry_solid=%d wave=%.0f moving=%d first_substeps=%s snapshots=%d textures=%d difference_count=%d"),
			Case.Label,
			Case.Resolution,
			Case.bOpensWaterBorders ? 1 : 0,
			Case.bTreatsDryCellsAsSolid ? 1 : 0,
			Case.RippleWaveSpeedCmPerSec,
			Case.bShiftsWindow ? 1 : 0,
			*SubstepSeries,
			Runs[0].Snapshots.Num(),
			ComparedTextures.Num(),
			TotalDifferenceCount);
		UE_LOG(LogTDFluidGpuWaterModuleRhiTests, Display, TEXT("%s"), *CaseLine);
		AddInfo(CaseLine);
	}

	TestTrue(TEXT("The scenes include odd ripple substep counts"), bSawOddRippleSubstepCount);
	TestTrue(TEXT("The scenes include even ripple substep counts"), bSawEvenRippleSubstepCount);
	return !HasAnyErrors();
}

#endif
