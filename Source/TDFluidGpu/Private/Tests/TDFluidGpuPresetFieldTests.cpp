#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicRHI.h"
#include "HAL/IConsoleManager.h"
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

namespace TDFluidGpuPresetFieldTestPrivate
{
	const TCHAR* const PresetFieldSolverVariableName = TEXT("TD.FluidFX.GpuPressureSolver");
	const TCHAR* const PresetFieldCyclesVariableName = TEXT("TD.FluidFX.GpuPressureCycles");
	const TCHAR* const PresetFieldWaterModeVariableName = TEXT("TD.FluidFX.WaterGpuPressureMode");
	const TCHAR* const PresetFieldKernelRadiusVariableName = TEXT("TD.FluidFX.WaterGpuKernelRadius");
	const TCHAR* const PresetFieldKernelFeedbackVariableName = TEXT("TD.FluidFX.WaterGpuKernelFeedback");
	const TCHAR* const PresetFieldKernelScaleVariableName = TEXT("TD.FluidFX.WaterGpuKernelScale");
	constexpr uint8 PresetFieldJacobiRequest = 1;
	constexpr uint8 PresetFieldMultigridFrom64Request = 2;

	class FTDPresetFieldConsoleScope
	{
	public:
		FTDPresetFieldConsoleScope(const TCHAR* Name, const TCHAR* Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (!Variable)
			{
				return;
			}

			PreviousValue = Variable->GetString();
			Variable->Set(Value, ECVF_SetByCode);
			bIsApplied = Variable->GetString() == FString(Value) || FMath::IsNearlyEqual(Variable->GetFloat(), FCString::Atof(Value));
		}

		~FTDPresetFieldConsoleScope()
		{
			if (!Variable)
			{
				return;
			}

			Variable->Set(*PreviousValue, ECVF_SetByCode);
		}

		FTDPresetFieldConsoleScope(const FTDPresetFieldConsoleScope&) = delete;
		FTDPresetFieldConsoleScope& operator=(const FTDPresetFieldConsoleScope&) = delete;

		bool IsValid() const
		{
			return Variable != nullptr && bIsApplied;
		}

	private:
		IConsoleVariable* Variable = nullptr;
		FString PreviousValue;
		bool bIsApplied = false;
	};

	struct FTDPresetFieldConsoleSet
	{
		FTDPresetFieldConsoleScope Solver;
		FTDPresetFieldConsoleScope Cycles;
		FTDPresetFieldConsoleScope WaterMode;
		FTDPresetFieldConsoleScope KernelRadius;
		FTDPresetFieldConsoleScope KernelFeedback;
		FTDPresetFieldConsoleScope KernelScale;

		FTDPresetFieldConsoleSet(const TCHAR* SolverValue, const TCHAR* CyclesValue, const TCHAR* WaterModeValue, const TCHAR* RadiusValue, const TCHAR* FeedbackValue, const TCHAR* ScaleValue)
			: Solver(PresetFieldSolverVariableName, SolverValue)
			, Cycles(PresetFieldCyclesVariableName, CyclesValue)
			, WaterMode(PresetFieldWaterModeVariableName, WaterModeValue)
			, KernelRadius(PresetFieldKernelRadiusVariableName, RadiusValue)
			, KernelFeedback(PresetFieldKernelFeedbackVariableName, FeedbackValue)
			, KernelScale(PresetFieldKernelScaleVariableName, ScaleValue)
		{
		}

		bool IsValid() const
		{
			return Solver.IsValid() && Cycles.IsValid() && WaterMode.IsValid() && KernelRadius.IsValid() && KernelFeedback.IsValid() && KernelScale.IsValid();
		}
	};

	FTDFluidGpuSimulationSettings MakePresetFieldSettings(int32 Resolution, bool bUsesWaterLayer)
	{
		FTDFluidGpuSimulationSettings Settings;
		Settings.Resolution = Resolution;
		Settings.ExtentCm = FVector2D(4000.0, 4000.0);
		Settings.bUsesWaterLayer = bUsesWaterLayer;
		return Settings;
	}

	FTDFluidGpuSimulationSettings ApplyOverridesToCopy(const FTDFluidGpuSimulationSettings& Settings)
	{
		FTDFluidGpuSimulationSettings Overridden = Settings;
		FTDFluidGpuBatch::ApplyWaterConsoleOverrides(Overridden);
		return Overridden;
	}

	bool HaveSameWaterShape(const FTDFluidGpuSimulationSettings& First, const FTDFluidGpuSimulationSettings& Second)
	{
		return First.WaterPressureModeRequest == Second.WaterPressureModeRequest
			&& First.WaterKernelRadius == Second.WaterKernelRadius
			&& FMath::IsNearlyEqual(First.WaterKernelFeedback, Second.WaterKernelFeedback, 0.0f)
			&& FMath::IsNearlyEqual(First.WaterKernelScale, Second.WaterKernelScale, 0.0f)
			&& First.WaterMultigridCycles == Second.WaterMultigridCycles;
	}

	constexpr int32 PresetFieldRhiResolution = 1024;
	constexpr int32 PresetFieldRhiFrameCount = 12;
	constexpr uint64 PresetFieldRhiActorKey = 4301;
	constexpr float PresetFieldRhiStepSeconds = 1.0f / 60.0f;
	constexpr float PresetFieldRhiSurfaceHeightCm = 100.0f;
	constexpr float PresetFieldRhiFlowSpeedCmPerSec = 150.0f;
	constexpr float PresetFieldRhiRockRadiusCm = 216.0f;
	constexpr int32 PresetFieldRhiChannelStartTenths = 3;
	constexpr int32 PresetFieldRhiChannelEndTenths = 7;
	constexpr double PresetFieldRhiStampPhasePerFrame = 0.31;
	const TCHAR* const PresetFieldRhiTextureLabels[] = { TEXT("Velocity"), TEXT("Density"), TEXT("Ripple"), TEXT("Pressure") };

	struct FTDPresetFieldTextureCapture
	{
		int32 Resolution = 0;
		EPixelFormat Format = PF_Unknown;
		TArray<uint8> Bytes;
	};

	struct FTDPresetFieldWaterRunResult
	{
		bool bHasWaterBatch = false;
		bool bHasReadAllTextures = false;
		FTDFluidGpuBatchKey WaterKey;
		TArray<FTDPresetFieldTextureCapture> Textures;
	};

	struct FTDPresetFieldWaterRunState
	{
		TArray<FTDFluidGpuBatch> Batches;
		TArray<TUniquePtr<FRHIGPUTextureReadback>> Readbacks;
		FTDPresetFieldWaterRunResult Result;
	};

	bool CanRunPresetFieldRhiTest(FAutomationTestBase& Test, const TCHAR* TestLabel)
	{
		if (!GUsingNullRHI && FApp::CanEverRender())
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), TestLabel));
		return false;
	}

	FTDFluidGpuSimulationSettings MakePresetFieldWaterSettings()
	{
		FTDFluidGpuSimulationSettings Settings = MakePresetFieldSettings(PresetFieldRhiResolution, true);
		Settings.SimMode = ETDFluidGpuSimMode::FluidSim;
		Settings.PressureIterations = 12;
		Settings.VorticityStrength = 0.3f;
		Settings.TerrainSlopeGravity = 0.0f;
		Settings.bOpenWaterBorders = true;
		Settings.bTreatDryCellsAsSolid = true;
		Settings.RippleWaveSpeedCmPerSec = 120.0f;
		return Settings;
	}

	void AddPresetFieldWaterChannel(const FTDFluidGpuSimulationSettings& Settings, FTDFluidGpuActorInput& InOutActor)
	{
		const int32 WaterTier = FTDFluidGpuBatch::ResolveWaterResolutionTier(Settings.Resolution);
		const int32 FieldResolution = WaterTier / FTDFluidGpuBatch::ComputeWaterFieldDivisor(WaterTier);
		for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
		{
			const bool bIsChannelRow = FieldY >= FieldResolution * PresetFieldRhiChannelStartTenths / 10 && FieldY < FieldResolution * PresetFieldRhiChannelEndTenths / 10;
			for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
			{
				FTDFluidGpuWaterGuideCellUpdate& GuideUpdate = InOutActor.WaterGuideUpdates.AddDefaulted_GetRef();
				GuideUpdate.FieldCell = FIntPoint(FieldX, FieldY);
				GuideUpdate.SurfaceHeightCm = bIsChannelRow ? PresetFieldRhiSurfaceHeightCm : TDFluidGpuSolverConstants::WaterNoSurfaceHeightCm;
				GuideUpdate.TargetVelocityCmPerSec = bIsChannelRow ? FVector2f(PresetFieldRhiFlowSpeedCmPerSec, 0.0f) : FVector2f::ZeroVector;
				GuideUpdate.GuideWeight = bIsChannelRow ? 1.0f : 0.0f;
				GuideUpdate.GuideDescent = 0.0f;

				FTDFluidGpuTerrainCellUpdate& TerrainUpdate = InOutActor.WaterTerrainUpdates.AddDefaulted_GetRef();
				TerrainUpdate.Cell = FIntPoint(FieldX, FieldY);
			}
		}
	}

	FTDFluidGpuActorInput MakePresetFieldWaterActor(const FTDFluidGpuSimulationSettings& Settings, int32 FrameIndex)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = PresetFieldRhiActorKey;
		Actor.Settings = Settings;
		Actor.DeltaTimeSeconds = PresetFieldRhiStepSeconds;
		Actor.SimulationTimeSeconds = FrameIndex * static_cast<double>(PresetFieldRhiStepSeconds);
		Actor.bHasObstacleListChanged = FrameIndex == 0;
		Actor.Obstacles.AddDefaulted_GetRef().RadiusCm = PresetFieldRhiRockRadiusCm;
		if (FrameIndex == 0)
		{
			AddPresetFieldWaterChannel(Settings, Actor);
		}

		const double CellSizeCm = Settings.ExtentCm.X / FTDFluidGpuBatch::ResolveWaterResolutionTier(Settings.Resolution);
		const double Phase = FrameIndex * PresetFieldRhiStampPhasePerFrame;
		FTDFluidGpuStampInput& Stamp = Actor.Stamps.AddDefaulted_GetRef();
		Stamp.CenterPlaneCm = FVector2D(500.0 * FMath::Cos(Phase), 200.0 * FMath::Sin(Phase));
		Stamp.VelocityPlaneCmPerSec = FVector2D(-200.0 * FMath::Sin(Phase), 200.0 * FMath::Cos(Phase));
		Stamp.RadiusCm = 3.0f * static_cast<float>(CellSizeCm);
		Stamp.DensityStrength = 0.6f;
		Stamp.VelocityStrength = 0.5f;
		Stamp.RippleImpulseCm = 1.5f;
		return Actor;
	}

	void EnqueuePresetFieldWaterCapture(FRHICommandListImmediate& RHICmdList, FTDPresetFieldWaterRunState& InOutRunState)
	{
		const FTDFluidGpuBatch* WaterBatch = InOutRunState.Batches.FindByPredicate([](const FTDFluidGpuBatch& Batch)
		{
			return Batch.GetKey().bUsesWaterLayer && !Batch.GetFrame().Parameters.IsEmpty();
		});
		if (!WaterBatch)
		{
			return;
		}

		const FTDFluidGpuBatchStateTextures& State = WaterBatch->StateTextures;
		const TRefCountPtr<IPooledRenderTarget>* const Sources[] = { &State.Velocity[0], &State.Density[0], &State.Ripple[0], &State.PressureSolution[0] };
		const uint32 SliceIndex = static_cast<uint32>(WaterBatch->GetFrame().Parameters[0].SliceIndex);
		InOutRunState.Result.bHasWaterBatch = true;
		InOutRunState.Result.WaterKey = WaterBatch->GetKey();
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuPresetFieldWaterCapture"));
		for (const TRefCountPtr<IPooledRenderTarget>* const Source : Sources)
		{
			FTDPresetFieldTextureCapture& Capture = InOutRunState.Result.Textures.AddDefaulted_GetRef();
			TUniquePtr<FRHIGPUTextureReadback>& Readback = InOutRunState.Readbacks.AddDefaulted_GetRef();
			if (!Source->IsValid())
			{
				continue;
			}

			Capture.Resolution = (*Source)->GetDesc().Extent.X;
			Capture.Format = (*Source)->GetDesc().Format;
			Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.PresetFieldWaterReadback"));
			AddEnqueueCopyPass(GraphBuilder, Readback.Get(), GraphBuilder.RegisterExternalTexture(*Source, TEXT("TDFluidGpu.PresetFieldWaterSource")), FResolveRect(), SliceIndex);
		}
		GraphBuilder.Execute();
	}

	bool ReadPresetFieldTexture(FRHIGPUTextureReadback* Readback, FTDPresetFieldTextureCapture& InOutCapture)
	{
		if (!Readback || !Readback->IsReady() || InOutCapture.Resolution <= 0 || InOutCapture.Format == PF_Unknown)
		{
			return false;
		}

		const int32 BytesPerTexel = GPixelFormats[InOutCapture.Format].BlockBytes;
		const int32 RowBytes = InOutCapture.Resolution * BytesPerTexel;
		int32 RowPitchInPixels = 0;
		int32 BufferHeight = 0;
		const uint8* Texels = static_cast<const uint8*>(Readback->Lock(RowPitchInPixels, &BufferHeight));
		if (!Texels || RowPitchInPixels < InOutCapture.Resolution || BufferHeight < InOutCapture.Resolution)
		{
			Readback->Unlock();
			return false;
		}

		InOutCapture.Bytes.SetNumUninitialized(InOutCapture.Resolution * RowBytes);
		for (int32 RowIndex = 0; RowIndex < InOutCapture.Resolution; ++RowIndex)
		{
			FMemory::Memcpy(InOutCapture.Bytes.GetData() + static_cast<int64>(RowIndex) * RowBytes, Texels + static_cast<int64>(RowIndex) * RowPitchInPixels * BytesPerTexel, RowBytes);
		}
		Readback->Unlock();
		return true;
	}

	bool ReadPresetFieldWaterCapture(FTDPresetFieldWaterRunState& InOutRunState)
	{
		TArray<FTDPresetFieldTextureCapture>& Textures = InOutRunState.Result.Textures;
		if (Textures.IsEmpty() || Textures.Num() != InOutRunState.Readbacks.Num())
		{
			return false;
		}

		for (int32 TextureIndex = 0; TextureIndex < Textures.Num(); ++TextureIndex)
		{
			if (!ReadPresetFieldTexture(InOutRunState.Readbacks[TextureIndex].Get(), Textures[TextureIndex]))
			{
				return false;
			}
		}
		return true;
	}

	FTDPresetFieldWaterRunResult RunPresetFieldWaterFrames(const FTDFluidGpuSimulationSettings& Settings)
	{
		TSharedRef<FTDPresetFieldWaterRunState, ESPMode::ThreadSafe> RunState = MakeShared<FTDPresetFieldWaterRunState, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuPresetFieldWaterFrames)(
			[RunState, Settings](FRHICommandListImmediate& RHICmdList)
			{
				for (int32 FrameIndex = 0; FrameIndex < PresetFieldRhiFrameCount; ++FrameIndex)
				{
					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
					FrameInputs.Actors.Add(MakePresetFieldWaterActor(Settings, FrameIndex));
					{
						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuPresetFieldWaterFrame"));
						AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, RunState->Batches);
						GraphBuilder.Execute();
					}
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}

				EnqueuePresetFieldWaterCapture(RHICmdList, *RunState);
				RHICmdList.SubmitAndBlockUntilGPUIdle();
				RunState->Result.bHasReadAllTextures = ReadPresetFieldWaterCapture(*RunState);
				RunState->Readbacks.Empty();
				RunState->Batches.Empty();
			});
		FlushRenderingCommands();
		return RunState->Result;
	}

	bool AreCapturesBitIdentical(const FTDPresetFieldTextureCapture& First, const FTDPresetFieldTextureCapture& Second)
	{
		return First.Resolution == Second.Resolution
			&& First.Format == Second.Format
			&& First.Bytes.Num() == Second.Bytes.Num()
			&& FMemory::Memcmp(First.Bytes.GetData(), Second.Bytes.GetData(), First.Bytes.Num()) == 0;
	}

	bool HasNonzeroByte(const FTDPresetFieldTextureCapture& Capture)
	{
		return Capture.Bytes.ContainsByPredicate([](uint8 Byte)
		{
			return Byte != 0;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuPresetFieldDefaultsMatchOldConsoleDefaultsTest,
	"TDGame.FluidFX.Gpu.PresetFields.DefaultsMatchOldConsoleDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuPresetFieldDefaultsMatchOldConsoleDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuPresetFieldTestPrivate;

	const int32 Resolutions[] = { 64, 256, 512, 1024 };
	for (const int32 Resolution : Resolutions)
	{
		for (const bool bUsesWaterLayer : { false, true })
		{
			const FTDFluidGpuSimulationSettings Settings = MakePresetFieldSettings(Resolution, bUsesWaterLayer);
			FTDFluidGpuSimulationSettings PresetSettings;
			FTDFluidGpuBatchKey PresetKey;
			{
				const FTDPresetFieldConsoleSet PresetConsole(TEXT("-1"), TEXT("-1"), TEXT("-1"), TEXT("-1"), TEXT("-1"), TEXT("-1"));
				TestTrue(TEXT("Preset console values are settable"), PresetConsole.IsValid());
				PresetSettings = ApplyOverridesToCopy(Settings);
				PresetKey = FTDFluidGpuBatch::MakeKey(PresetSettings);
			}

			const FTDPresetFieldConsoleSet ForcedConsole(TEXT("0"), TEXT("1"), TEXT("0"), TEXT("4"), TEXT("0.98"), TEXT("1.0"));
			TestTrue(TEXT("Forced console values are settable"), ForcedConsole.IsValid());
			const FTDFluidGpuSimulationSettings ForcedSettings = ApplyOverridesToCopy(Settings);
			const FString Label = FString::Printf(TEXT("%d %s"), Resolution, bUsesWaterLayer ? TEXT("water") : TEXT("fluid"));
			TestTrue(*(Label + TEXT(" preset key equals the old console default key")), FTDFluidGpuBatch::MakeKey(ForcedSettings) == PresetKey);
			TestTrue(*(Label + TEXT(" preset water shape equals the old console defaults")), HaveSameWaterShape(PresetSettings, ForcedSettings));
			TestEqual(*(Label + TEXT(" memory estimate keeps the old default")), FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, Resolution).GetTotalBytes(), FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, Resolution, false, false, 0).GetTotalBytes());
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuPresetFieldReachesKeyAndConsoleWinsTest,
	"TDGame.FluidFX.Gpu.PresetFields.ReachKeyAndConsoleWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuPresetFieldReachesKeyAndConsoleWinsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuPresetFieldTestPrivate;

	FTDFluidGpuSimulationSettings MultigridFluid = MakePresetFieldSettings(256, false);
	MultigridFluid.PressureSolverModeRequest = PresetFieldMultigridFrom64Request;
	MultigridFluid.MultigridCyclesRequest = 3;
	FTDFluidGpuSimulationSettings JacobiFluid = MakePresetFieldSettings(512, false);
	JacobiFluid.PressureSolverModeRequest = PresetFieldJacobiRequest;
	FTDFluidGpuSimulationSettings ShapedWater = MakePresetFieldSettings(1024, true);
	ShapedWater.WaterKernelRadius = 6;
	ShapedWater.WaterKernelFeedback = 0.9f;
	ShapedWater.WaterKernelScale = 2.0f;
	ShapedWater.WaterMultigridCycles = 2;
	{
		const FTDPresetFieldConsoleSet PresetConsole(TEXT("-1"), TEXT("-1"), TEXT("-1"), TEXT("-1"), TEXT("-1"), TEXT("-1"));
		TestTrue(TEXT("Preset console values are settable"), PresetConsole.IsValid());
		const FTDFluidGpuBatchKey MultigridKey = FTDFluidGpuBatch::MakeKey(MultigridFluid);
		TestEqual(TEXT("Preset multigrid request reaches the 256 key"), static_cast<int32>(MultigridKey.PressureSolver), static_cast<int32>(ETDFluidGpuPressureSolver::Multigrid));
		TestEqual(TEXT("Preset cycle request reaches the key"), MultigridKey.MultigridCycles, 3);
		TestEqual(TEXT("Preset Jacobi request reaches the 512 key"), static_cast<int32>(FTDFluidGpuBatch::MakeKey(JacobiFluid).PressureSolver), static_cast<int32>(ETDFluidGpuPressureSolver::Jacobi));
		TestEqual(TEXT("Preset multigrid request reaches the memory estimate"), FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, 256, false, false, PresetFieldMultigridFrom64Request).TransientBytes, FTDFluidGpuBatch::ComputeMultigridTransientBytes(256));

		const FTDFluidGpuSimulationSettings PresetWater = ApplyOverridesToCopy(ShapedWater);
		TestEqual(TEXT("Preset kernel radius survives the console pass"), PresetWater.WaterKernelRadius, 6);
		TestEqual(TEXT("Preset kernel feedback survives the console pass"), PresetWater.WaterKernelFeedback, 0.9f);
		TestEqual(TEXT("Preset kernel scale survives the console pass"), PresetWater.WaterKernelScale, 2.0f);
		TestEqual(TEXT("Preset water cycles reach the water key"), FTDFluidGpuBatch::MakeKey(PresetWater).MultigridCycles, 2);
	}

	const FTDPresetFieldConsoleSet ForcedConsole(TEXT("1"), TEXT("4"), TEXT("0"), TEXT("3"), TEXT("0.5"), TEXT("1.5"));
	TestTrue(TEXT("Forced console values are settable"), ForcedConsole.IsValid());
	TestEqual(TEXT("Console Jacobi beats the preset multigrid request"), static_cast<int32>(FTDFluidGpuBatch::MakeKey(MultigridFluid).PressureSolver), static_cast<int32>(ETDFluidGpuPressureSolver::Jacobi));
	const FTDFluidGpuSimulationSettings ForcedWater = ApplyOverridesToCopy(ShapedWater);
	TestEqual(TEXT("Console kernel radius beats the preset"), ForcedWater.WaterKernelRadius, 3);
	TestEqual(TEXT("Console kernel feedback beats the preset"), ForcedWater.WaterKernelFeedback, 0.5f);
	TestEqual(TEXT("Console kernel scale beats the preset"), ForcedWater.WaterKernelScale, 1.5f);
	TestEqual(TEXT("Console cycles beat the preset water cycles"), FTDFluidGpuBatch::MakeKey(ForcedWater).MultigridCycles, 4);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuPresetFieldWaterRhiMatchesOldConsoleDefaultsTest,
	"TDGame.FluidFXRhi.Gpu.PresetFields.Water1024MatchesOldConsoleDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuPresetFieldWaterRhiMatchesOldConsoleDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuPresetFieldTestPrivate;

	if (!CanRunPresetFieldRhiTest(*this, TEXT("Gpu.PresetFields.Water1024MatchesOldConsoleDefaults")))
	{
		return false;
	}

	FTDFluidGpuSimulationSettings PresetSettings;
	{
		const FTDPresetFieldConsoleSet PresetConsole(TEXT("-1"), TEXT("-1"), TEXT("-1"), TEXT("-1"), TEXT("-1"), TEXT("-1"));
		if (!TestTrue(TEXT("Preset console values are settable"), PresetConsole.IsValid()))
		{
			return false;
		}
		PresetSettings = ApplyOverridesToCopy(MakePresetFieldWaterSettings());
	}

	FTDFluidGpuSimulationSettings ShiftedPreset = MakePresetFieldWaterSettings();
	ShiftedPreset.WaterPressureModeRequest = 1;
	ShiftedPreset.WaterKernelRadius = 6;
	ShiftedPreset.WaterKernelFeedback = 0.9f;
	ShiftedPreset.WaterKernelScale = 2.0f;
	ShiftedPreset.WaterMultigridCycles = 3;
	FTDFluidGpuSimulationSettings ForcedSettings;
	{
		const FTDPresetFieldConsoleSet ForcedConsole(TEXT("0"), TEXT("1"), TEXT("0"), TEXT("4"), TEXT("0.98"), TEXT("1.0"));
		if (!TestTrue(TEXT("Forced console values are settable"), ForcedConsole.IsValid()))
		{
			return false;
		}
		ForcedSettings = ApplyOverridesToCopy(ShiftedPreset);
	}
	TestTrue(TEXT("Forced old console defaults replace the shifted preset water shape"), HaveSameWaterShape(PresetSettings, ForcedSettings));

	const FTDPresetFieldWaterRunResult PresetRun = RunPresetFieldWaterFrames(PresetSettings);
	const FTDPresetFieldWaterRunResult ForcedRun = RunPresetFieldWaterFrames(ForcedSettings);
	const bool bHasBothRuns = PresetRun.bHasWaterBatch && PresetRun.bHasReadAllTextures && ForcedRun.bHasWaterBatch && ForcedRun.bHasReadAllTextures;
	const bool bHasEveryTexture = PresetRun.Textures.Num() == static_cast<int32>(UE_ARRAY_COUNT(PresetFieldRhiTextureLabels)) && ForcedRun.Textures.Num() == PresetRun.Textures.Num();
	if (!TestTrue(TEXT("Both 1024 water runs read every state texture"), bHasBothRuns && bHasEveryTexture))
	{
		return false;
	}

	TestTrue(TEXT("Both 1024 water runs use the same batch key"), PresetRun.WaterKey == ForcedRun.WaterKey);
	TestTrue(TEXT("The 1024 preset run moves water"), HasNonzeroByte(PresetRun.Textures[0]));
	for (int32 TextureIndex = 0; TextureIndex < PresetRun.Textures.Num(); ++TextureIndex)
	{
		TestTrue(*FString::Printf(TEXT("1024 water %s is bit identical between preset defaults and forced old console defaults"), PresetFieldRhiTextureLabels[TextureIndex]), AreCapturesBitIdentical(PresetRun.Textures[TextureIndex], ForcedRun.Textures[TextureIndex]));
	}
	return !HasAnyErrors();
}

#endif
