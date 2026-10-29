#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Level.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidSolverOptionRules.h"
#include "FluidFX/TDFluidTypes.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuStepPasses.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidSolverOptionsRhiTests, Log, All);

namespace TDFluidSolverOptionsRhiTestSupport
{
	constexpr int32 KernelParityResolution = 128;
	constexpr int32 KernelParityFrameCount = 120;
	constexpr int32 KernelParityStampFrameCount = 40;
	constexpr int32 FramesPerRenderCommand = 64;
	constexpr float KernelParityStepSeconds = 1.0f / 60.0f;
	constexpr uint64 KernelParityActorKey = 1501;
	constexpr double DensitySumTolerance = 0.03;
	constexpr double MeanSpeedTolerance = 0.05;

	using FTDFluidSolverOptionsFrameBuilder = TFunction<void(int32, FTDFluidGpuFrameInputs&)>;

	struct FTDFluidSolverOptionsGpuRunResult
	{
		bool bHasReadback = false;
		bool bUsedKernelBatch = false;
		FTDFluidGpuReadbackFrame Readback;
	};

	struct FTDFluidSolverOptionsGpuRunState
	{
		TArray<FTDFluidGpuBatch> Batches;
		TUniquePtr<FTDFluidGpuReadbackRing> ReadbackRing = MakeUnique<FTDFluidGpuReadbackRing>();
		FTDFluidSolverOptionsGpuRunResult Result;
	};

	struct FTDFluidSolverOptionsFieldStatistics
	{
		double DensitySum = 0.0;
		double MeanSpeed = 0.0;
	};

	bool CanRunSolverOptionsRhiTest(FAutomationTestBase& Test, const TCHAR* TestName)
	{
		const bool bHasRenderingRhi = !GUsingNullRHI && FApp::CanEverRender();
		UE_LOG(LogTDFluidSolverOptionsRhiTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), TestName, bHasRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (bHasRenderingRhi)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), TestName));
		return false;
	}

	FTDFluidSolverOptionsGpuRunResult RunSolverOptionsGpuFrames(int32 FrameCount, const FTDFluidSolverOptionsFrameBuilder& BuildFrame)
	{
		TSharedRef<FTDFluidSolverOptionsGpuRunState, ESPMode::ThreadSafe> RunState = MakeShared<FTDFluidSolverOptionsGpuRunState, ESPMode::ThreadSafe>();
		for (int32 FirstFrame = 0; FirstFrame < FrameCount; FirstFrame += FramesPerRenderCommand)
		{
			const int32 EndFrame = FMath::Min(FirstFrame + FramesPerRenderCommand, FrameCount);
			ENQUEUE_RENDER_COMMAND(TDFluidSolverOptionsRhiFrames)(
				[RunState, BuildFrame, FirstFrame, EndFrame, FrameCount](FRHICommandListImmediate& RHICmdList)
				{
					for (int32 FrameIndex = FirstFrame; FrameIndex < EndFrame; ++FrameIndex)
					{
						FTDFluidGpuFrameInputs FrameInputs;
						FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
						BuildFrame(FrameIndex, FrameInputs);

						FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidSolverOptionsRhiTest"));
						const FTDFluidGpuFrameOutputs Outputs = AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, RunState->Batches);
						if (FrameIndex == FrameCount - 1)
						{
							RunState->ReadbackRing->Enqueue(GraphBuilder, Outputs);
						}
						GraphBuilder.Execute();
						RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
					}

					if (EndFrame < FrameCount)
					{
						return;
					}

					RHICmdList.SubmitAndBlockUntilGPUIdle();
					RunState->Result.bHasReadback = RunState->ReadbackRing->TryReadLatest(RunState->Result.Readback);
					for (const FTDFluidGpuBatch& Batch : RunState->Batches)
					{
						RunState->Result.bUsedKernelBatch |= Batch.GetKey().PressureSolver == ETDFluidGpuPressureSolver::KernelFeedback;
					}
					RunState->Batches.Empty();
					RunState->ReadbackRing.Reset();
				});
			FlushRenderingCommands();
		}
		return RunState->Result;
	}

	FTDFluidSimulationConfig MakeKernelParityConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = KernelParityResolution;
		Config.SimExtentCm = FVector2D(1000.0, 1000.0);
		Config.DensityDissipation = 0.0f;
		Config.VorticityStrength = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.PressureMethod = ETDFluidPressureMethod::KernelFeedback;
		return Config;
	}

	FTDFluidBrushStamp MakeKernelParityStamp(const FVector2D& LocationCm, const FVector2D& VelocityCmPerSec, float RadiusCm, float DensityStrength)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(LocationCm.X, LocationCm.Y, 0.0);
		Stamp.WorldVelocity = FVector(VelocityCmPerSec.X, VelocityCmPerSec.Y, 0.0);
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = DensityStrength;
		Stamp.VelocityStrength = 1.0f;
		return Stamp;
	}

	TArray<FTDFluidBrushStamp> MakeKernelParityStamps()
	{
		TArray<FTDFluidBrushStamp> Stamps;
		Stamps.Add(MakeKernelParityStamp(FVector2D(-70.0, 0.0), FVector2D(-300.0, 0.0), 60.0f, 0.3f));
		Stamps.Add(MakeKernelParityStamp(FVector2D(70.0, 0.0), FVector2D(300.0, 0.0), 60.0f, 0.3f));
		Stamps.Add(MakeKernelParityStamp(FVector2D(0.0, 120.0), FVector2D(0.0, 250.0), 50.0f, 0.3f));
		return Stamps;
	}

	FTDFluidGpuActorInput MakeKernelParityActor(const FTDFluidSimulationConfig& Config, int32 FrameIndex)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = KernelParityActorKey;
		Actor.Settings = FTDFluidGpuSolverProxy::MakeSimulationSettings(Config, Config.GridResolution);
		Actor.DeltaTimeSeconds = KernelParityStepSeconds;
		Actor.WindowCenterPlaneCm = FVector2D::ZeroVector;
		if (FrameIndex >= KernelParityStampFrameCount)
		{
			return Actor;
		}

		for (const FTDFluidBrushStamp& Stamp : MakeKernelParityStamps())
		{
			Actor.Stamps.Add(FTDFluidGpuSolverProxy::MakeStampInput(Stamp));
		}
		return Actor;
	}

	FTDFluidSolverOptionsGpuRunResult RunKernelParityGpu(const FTDFluidSimulationConfig& Config)
	{
		return RunSolverOptionsGpuFrames(KernelParityFrameCount, [Config](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
		{
			OutFrame.Actors.Add(MakeKernelParityActor(Config, FrameIndex));
		});
	}

	FTDFluidSolverOptionsFieldStatistics MeasureFieldStatistics(const FTDFluidReadbackField& Field)
	{
		FTDFluidSolverOptionsFieldStatistics Statistics;
		for (const float Density : Field.PrimaryDensity)
		{
			Statistics.DensitySum += Density;
		}
		for (const FVector2f& Velocity : Field.VelocityCmPerSec)
		{
			Statistics.MeanSpeed += Velocity.Size();
		}
		if (Field.VelocityCmPerSec.Num() > 0)
		{
			Statistics.MeanSpeed /= static_cast<double>(Field.VelocityCmPerSec.Num());
		}
		return Statistics;
	}

	FTDFluidSolverOptionsFieldStatistics RunKernelParityCpu(const FTDFluidSimulationConfig& Config)
	{
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		const TArray<FTDFluidBrushStamp> Stamps = MakeKernelParityStamps();
		for (int32 FrameIndex = 0; FrameIndex < KernelParityFrameCount; ++FrameIndex)
		{
			if (FrameIndex < KernelParityStampFrameCount)
			{
				Solver.ApplyBrushStamps(Stamps, KernelParityStepSeconds);
			}
			Solver.StepSimulation(KernelParityStepSeconds);
		}

		FTDFluidReadbackField CpuField;
		CpuField.DownsampleFromSolver(Solver);
		return MeasureFieldStatistics(CpuField);
	}

	bool IsWithinRelativeTolerance(double Actual, double Expected, double Tolerance)
	{
		return FMath::Abs(Actual - Expected) <= Tolerance * FMath::Max(FMath::Abs(Expected), UE_DOUBLE_SMALL_NUMBER);
	}

	double ComputeRelativeGap(double Actual, double Expected)
	{
		return (Actual - Expected) / FMath::Max(FMath::Abs(Expected), UE_DOUBLE_SMALL_NUMBER);
	}

	FTDFluidSimulationConfig MakePoissonBaselineConfig()
	{
		FTDFluidSimulationConfig Config = MakeKernelParityConfig();
		Config.PressureMethod = ETDFluidPressureMethod::PoissonSolve;
		return Config;
	}

	bool MeasureGpuFieldStatistics(const FTDFluidSimulationConfig& Config, FTDFluidSolverOptionsGpuRunResult& OutRun, FTDFluidSolverOptionsFieldStatistics& OutStatistics)
	{
		OutRun = RunKernelParityGpu(Config);
		if (!OutRun.bHasReadback)
		{
			return false;
		}

		FTDFluidReadbackField GpuField;
		if (!GpuField.CopyFromAtlasTile(OutRun.Readback.Texels, OutRun.Readback.Layout.GetAtlasSize(), OutRun.Readback.Layout.GetTileOrigin(0), OutRun.Readback.FrameNumber, FVector2D::ZeroVector, Config.SimExtentCm))
		{
			return false;
		}

		OutStatistics = MeasureFieldStatistics(GpuField);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsKernelPressureMatchesCpuStatisticsTest,
	"TDGame.FluidFXRhi.SolverOptions.KernelPressureMatchesCpuStatistics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsKernelPressureMatchesCpuStatisticsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsRhiTestSupport;
	if (!CanRunSolverOptionsRhiTest(*this, TEXT("TDGame.FluidFXRhi.SolverOptions.KernelPressureMatchesCpuStatistics")))
	{
		return false;
	}

	const FTDFluidSimulationConfig Config = MakeKernelParityConfig();
	TestTrue(TEXT("The CPU solver can use kernel pressure for the parity window"), TDFluidSolverOptionRules::CanUseKernelPressure(Config));
	FTDFluidSolverOptionsGpuRunResult KernelRun;
	FTDFluidSolverOptionsFieldStatistics GpuStatistics;
	if (!MeasureGpuFieldStatistics(Config, KernelRun, GpuStatistics))
	{
		AddError(TEXT("The kernel GPU readback was not ready after the GPU idled or did not copy into a field"));
		return false;
	}
	TestTrue(TEXT("The GPU actor ran in a kernel feedback batch"), KernelRun.bUsedKernelBatch);

	const FTDFluidSimulationConfig BaselineConfig = MakePoissonBaselineConfig();
	FTDFluidSolverOptionsGpuRunResult BaselineRun;
	FTDFluidSolverOptionsFieldStatistics GpuBaselineStatistics;
	if (!MeasureGpuFieldStatistics(BaselineConfig, BaselineRun, GpuBaselineStatistics))
	{
		AddError(TEXT("The Poisson baseline GPU readback was not ready after the GPU idled or did not copy into a field"));
		return false;
	}
	TestFalse(TEXT("The Poisson baseline GPU actor ran outside the kernel feedback batch"), BaselineRun.bUsedKernelBatch);

	const FTDFluidSolverOptionsFieldStatistics CpuStatistics = RunKernelParityCpu(Config);
	const FTDFluidSolverOptionsFieldStatistics CpuBaselineStatistics = RunKernelParityCpu(BaselineConfig);
	const double KernelSpeedGap = ComputeRelativeGap(GpuStatistics.MeanSpeed, CpuStatistics.MeanSpeed);
	const double BaselineSpeedGap = ComputeRelativeGap(GpuBaselineStatistics.MeanSpeed, CpuBaselineStatistics.MeanSpeed);
	const FString StatisticsLine = FString::Printf(TEXT("TDFluidSolverOptionsKernelParity: gpu_density_sum=%.4f cpu_density_sum=%.4f gpu_mean_speed=%.4f cpu_mean_speed=%.4f kernel_speed_gap=%.4f"), GpuStatistics.DensitySum, CpuStatistics.DensitySum, GpuStatistics.MeanSpeed, CpuStatistics.MeanSpeed, KernelSpeedGap);
	const FString BaselineLine = FString::Printf(TEXT("TDFluidSolverOptionsPoissonBaseline: gpu_density_sum=%.4f cpu_density_sum=%.4f gpu_mean_speed=%.4f cpu_mean_speed=%.4f baseline_speed_gap=%.4f kernel_minus_baseline=%.4f"), GpuBaselineStatistics.DensitySum, CpuBaselineStatistics.DensitySum, GpuBaselineStatistics.MeanSpeed, CpuBaselineStatistics.MeanSpeed, BaselineSpeedGap, KernelSpeedGap - BaselineSpeedGap);
	UE_LOG(LogTDFluidSolverOptionsRhiTests, Display, TEXT("%s"), *StatisticsLine);
	UE_LOG(LogTDFluidSolverOptionsRhiTests, Display, TEXT("%s"), *BaselineLine);
	AddInfo(StatisticsLine);
	AddInfo(BaselineLine);
	TestTrue(TEXT("The CPU kernel run holds density"), CpuStatistics.DensitySum > 0.0);
	TestTrue(TEXT("The CPU kernel run keeps moving"), CpuStatistics.MeanSpeed > 0.0);
	TestTrue(TEXT("The CPU Poisson baseline run keeps moving"), CpuBaselineStatistics.MeanSpeed > 0.0);
	TestTrue(TEXT("GPU kernel density sum is within 3% of the CPU kernel run"), IsWithinRelativeTolerance(GpuStatistics.DensitySum, CpuStatistics.DensitySum, DensitySumTolerance));
	TestTrue(TEXT("The GPU to CPU mean speed gap of the kernel stays within 5% of the same gap under the Poisson baseline"), FMath::Abs(KernelSpeedGap - BaselineSpeedGap) <= MeanSpeedTolerance);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsKernelPressureRunToRunDeterministicTest,
	"TDGame.FluidFXRhi.SolverOptions.KernelPressureRunToRunDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsKernelPressureRunToRunDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsRhiTestSupport;
	if (!CanRunSolverOptionsRhiTest(*this, TEXT("TDGame.FluidFXRhi.SolverOptions.KernelPressureRunToRunDeterministic")))
	{
		return false;
	}

	const FTDFluidSimulationConfig Config = MakeKernelParityConfig();
	const FTDFluidSolverOptionsGpuRunResult FirstRun = RunKernelParityGpu(Config);
	const FTDFluidSolverOptionsGpuRunResult SecondRun = RunKernelParityGpu(Config);
	if (!FirstRun.bHasReadback || !SecondRun.bHasReadback)
	{
		AddError(TEXT("Readback atlas was not ready after the GPU idled"));
		return false;
	}

	TestTrue(TEXT("Both runs used a kernel feedback batch"), FirstRun.bUsedKernelBatch && SecondRun.bUsedKernelBatch);
	const TArray<FFloat16Color>& FirstTexels = FirstRun.Readback.Texels;
	const TArray<FFloat16Color>& SecondTexels = SecondRun.Readback.Texels;
	TestTrue(TEXT("Two kernel runs produce bit identical readback tiles"), FirstTexels.Num() > 0 && FirstTexels.Num() == SecondTexels.Num() && FMemory::Memcmp(FirstTexels.GetData(), SecondTexels.GetData(), FirstTexels.Num() * sizeof(FFloat16Color)) == 0);
	return !HasAnyErrors();
}

namespace TDFluidSolverOptionsRhiTestSupport
{
	constexpr uint64 MixedBatchOptionActorKey = 1502;
	constexpr double ForceStatisticsTolerance = 0.05;
	constexpr double CentroidMotionTolerance = 0.05;
	constexpr float ForceOptionDampingPerSec = 2.0f;
	constexpr float ForceOptionNoiseScaleCm = 200.0f;
	const FVector2D ForceOptionAdvectionOffsetCmPerSec(60.0, 0.0);
	const FIntPoint ForceOptionLatticeOffsetCells(37, 911);
	const TCHAR* const SolverOptionsStageMapPackage = TEXT("/Game/FluidFX/Maps/LV_TDFluidSolverOptionsStage");

	struct FTDFluidSolverOptionsForceRun
	{
		FTDFluidSolverOptionsFieldStatistics Statistics;
		double DensityCentroidX = 0.0;
	};

	FTDFluidSimulationConfig MakeForceOptionsConfig(bool bUsesForceOptions)
	{
		FTDFluidSimulationConfig Config = MakePoissonBaselineConfig();
		if (bUsesForceOptions)
		{
			Config.FlowAmplitudeNoiseDampingPerSec = ForceOptionDampingPerSec;
			Config.FlowAmplitudeNoiseScaleCm = ForceOptionNoiseScaleCm;
		}
		return Config;
	}

	FTDFluidGpuActorInput MakeForceOptionsActor(const FTDFluidSimulationConfig& Config, bool bUsesForceOptions, uint64 ActorKey, int32 FrameIndex)
	{
		FTDFluidGpuActorInput Actor = MakeKernelParityActor(Config, FrameIndex);
		Actor.ActorKey = ActorKey;
		Actor.SimulationTimeSeconds = static_cast<double>(FrameIndex) * KernelParityStepSeconds;
		if (bUsesForceOptions)
		{
			Actor.Settings.SolverOptions.AdvectionOffsetPlaneCmPerSec = ForceOptionAdvectionOffsetCmPerSec;
			Actor.Settings.SolverOptions.NoiseLatticeOffsetCells = ForceOptionLatticeOffsetCells;
		}
		return Actor;
	}

	double MeasureDensityCentroidX(const FTDFluidReadbackField& Field, double ExtentX)
	{
		const int32 Resolution = FTDFluidReadbackField::GetResolution();
		double WeightedX = 0.0;
		double DensityWeight = 0.0;
		for (int32 CellIndex = 0; CellIndex < Field.PrimaryDensity.Num(); ++CellIndex)
		{
			const double Density = Field.PrimaryDensity[CellIndex];
			const double NormalizedX = (static_cast<double>(CellIndex % Resolution) + 0.5) / Resolution - 0.5;
			WeightedX += Density * NormalizedX * ExtentX;
			DensityWeight += Density;
		}
		return DensityWeight > 0.0 ? WeightedX / DensityWeight : 0.0;
	}

	FTDFluidSolverOptionsForceRun MeasureForceRun(const FTDFluidReadbackField& Field, double ExtentX)
	{
		FTDFluidSolverOptionsForceRun Run;
		Run.Statistics = MeasureFieldStatistics(Field);
		Run.DensityCentroidX = MeasureDensityCentroidX(Field, ExtentX);
		return Run;
	}

	FTDFluidSolverOptionsForceRun RunForceOptionsCpu(bool bUsesForceOptions)
	{
		const FTDFluidSimulationConfig Config = MakeForceOptionsConfig(bUsesForceOptions);
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		if (bUsesForceOptions)
		{
			Solver.SetAdvectionOffsetPlane(ForceOptionAdvectionOffsetCmPerSec);
			Solver.SetNoiseLatticeOffsetCells(ForceOptionLatticeOffsetCells);
		}

		const TArray<FTDFluidBrushStamp> Stamps = MakeKernelParityStamps();
		for (int32 FrameIndex = 0; FrameIndex < KernelParityFrameCount; ++FrameIndex)
		{
			if (FrameIndex < KernelParityStampFrameCount)
			{
				Solver.ApplyBrushStamps(Stamps, KernelParityStepSeconds);
			}
			Solver.StepSimulation(KernelParityStepSeconds);
		}

		FTDFluidReadbackField CpuField;
		CpuField.DownsampleFromSolver(Solver);
		return MeasureForceRun(CpuField, Config.SimExtentCm.X);
	}

	bool CopyGpuTile(const FTDFluidSolverOptionsGpuRunResult& Run, int32 ActorInputIndex, const FVector2D& ExtentCm, FTDFluidReadbackField& OutField)
	{
		return Run.bHasReadback && OutField.CopyFromAtlasTile(Run.Readback.Texels, Run.Readback.Layout.GetAtlasSize(), Run.Readback.Layout.GetTileOrigin(ActorInputIndex), Run.Readback.FrameNumber, FVector2D::ZeroVector, ExtentCm);
	}

	bool RunForceOptionsGpu(bool bUsesForceOptions, FTDFluidSolverOptionsForceRun& OutRun)
	{
		const FTDFluidSimulationConfig Config = MakeForceOptionsConfig(bUsesForceOptions);
		const FTDFluidSolverOptionsGpuRunResult Run = RunSolverOptionsGpuFrames(KernelParityFrameCount, [Config, bUsesForceOptions](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
		{
			OutFrame.Actors.Add(MakeForceOptionsActor(Config, bUsesForceOptions, KernelParityActorKey, FrameIndex));
		});

		FTDFluidReadbackField GpuField;
		if (!CopyGpuTile(Run, 0, Config.SimExtentCm, GpuField))
		{
			return false;
		}
		OutRun = MeasureForceRun(GpuField, Config.SimExtentCm.X);
		return true;
	}

	bool RunMixedBatchGpu(bool bIncludesOptionActor, FTDFluidReadbackField& OutPlainField, FTDFluidReadbackField& OutOptionField)
	{
		const FTDFluidSimulationConfig PlainConfig = MakeForceOptionsConfig(false);
		const FTDFluidSimulationConfig OptionConfig = MakeForceOptionsConfig(true);
		const FTDFluidSolverOptionsGpuRunResult Run = RunSolverOptionsGpuFrames(KernelParityFrameCount, [PlainConfig, OptionConfig, bIncludesOptionActor](int32 FrameIndex, FTDFluidGpuFrameInputs& OutFrame)
		{
			OutFrame.Actors.Add(MakeForceOptionsActor(PlainConfig, false, KernelParityActorKey, FrameIndex));
			if (bIncludesOptionActor)
			{
				OutFrame.Actors.Add(MakeForceOptionsActor(OptionConfig, true, MixedBatchOptionActorKey, FrameIndex));
			}
		});

		if (!CopyGpuTile(Run, 0, PlainConfig.SimExtentCm, OutPlainField))
		{
			return false;
		}
		return !bIncludesOptionActor || CopyGpuTile(Run, 1, OptionConfig.SimExtentCm, OutOptionField);
	}

	template <typename ElementType>
	bool AreArraysBitIdentical(const TArray<ElementType>& First, const TArray<ElementType>& Second)
	{
		return First.Num() == Second.Num() && FMemory::Memcmp(First.GetData(), Second.GetData(), First.Num() * sizeof(ElementType)) == 0;
	}

	bool AreFieldsBitIdentical(const FTDFluidReadbackField& First, const FTDFluidReadbackField& Second)
	{
		return AreArraysBitIdentical(First.PrimaryDensity, Second.PrimaryDensity)
			&& AreArraysBitIdentical(First.SelectiveDensity, Second.SelectiveDensity)
			&& AreArraysBitIdentical(First.VelocityCmPerSec, Second.VelocityCmPerSec);
	}

	FString MakePresetObjectPath(ETDFluidPresetKind Kind)
	{
		const FString KindName = StaticEnum<ETDFluidPresetKind>()->GetNameStringByValue(static_cast<int64>(Kind));
		return FString::Printf(TEXT("/Game/FluidFX/Presets/DA_TDFluid_%s.DA_TDFluid_%s"), *KindName, *KindName);
	}

	bool IsAnchoredStageActor(const AActor* Actor)
	{
		const UTDFluidLiveComponent* LiveComponent = Actor ? Actor->FindComponentByClass<UTDFluidLiveComponent>() : nullptr;
		return LiveComponent && LiveComponent->ActivePreset && LiveComponent->ActivePreset->PresetKind == ETDFluidPresetKind::SmokeAnchoredCauldron;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsForceOptionsMatchCpuStatisticsTest,
	"TDGame.FluidFXRhi.SolverOptions.ForceOptionsMatchCpuStatistics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsForceOptionsMatchCpuStatisticsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsRhiTestSupport;
	if (!CanRunSolverOptionsRhiTest(*this, TEXT("TDGame.FluidFXRhi.SolverOptions.ForceOptionsMatchCpuStatistics")))
	{
		return false;
	}

	FTDFluidSolverOptionsForceRun GpuOptions;
	FTDFluidSolverOptionsForceRun GpuBaseline;
	if (!RunForceOptionsGpu(true, GpuOptions) || !RunForceOptionsGpu(false, GpuBaseline))
	{
		AddError(TEXT("The force option GPU readback was not ready after the GPU idled or did not copy into a field"));
		return false;
	}

	const FTDFluidSolverOptionsForceRun CpuOptions = RunForceOptionsCpu(true);
	const FTDFluidSolverOptionsForceRun CpuBaseline = RunForceOptionsCpu(false);
	const double GpuCentroidMotion = GpuOptions.DensityCentroidX - GpuBaseline.DensityCentroidX;
	const double CpuCentroidMotion = CpuOptions.DensityCentroidX - CpuBaseline.DensityCentroidX;
	const double OptionSpeedGap = ComputeRelativeGap(GpuOptions.Statistics.MeanSpeed, CpuOptions.Statistics.MeanSpeed);
	const double BaselineSpeedGap = ComputeRelativeGap(GpuBaseline.Statistics.MeanSpeed, CpuBaseline.Statistics.MeanSpeed);
	const FString StatisticsLine = FString::Printf(TEXT("TDFluidSolverOptionsForceParity: gpu_density_sum=%.4f cpu_density_sum=%.4f gpu_mean_speed=%.4f cpu_mean_speed=%.4f option_speed_gap=%.4f baseline_speed_gap=%.4f gpu_centroid_motion=%.3f cpu_centroid_motion=%.3f"), GpuOptions.Statistics.DensitySum, CpuOptions.Statistics.DensitySum, GpuOptions.Statistics.MeanSpeed, CpuOptions.Statistics.MeanSpeed, OptionSpeedGap, BaselineSpeedGap, GpuCentroidMotion, CpuCentroidMotion);
	UE_LOG(LogTDFluidSolverOptionsRhiTests, Display, TEXT("%s"), *StatisticsLine);
	AddInfo(StatisticsLine);
	TestTrue(TEXT("The CPU advection offset moves the density centroid forward"), CpuCentroidMotion > 0.0);
	TestTrue(TEXT("The CPU amplitude noise removes speed"), CpuOptions.Statistics.MeanSpeed < CpuBaseline.Statistics.MeanSpeed);
	TestTrue(TEXT("GPU option density sum is within 5% of the CPU option run"), IsWithinRelativeTolerance(GpuOptions.Statistics.DensitySum, CpuOptions.Statistics.DensitySum, ForceStatisticsTolerance));
	TestTrue(TEXT("The GPU to CPU mean speed gap with options stays within 5% of the same gap without options"), FMath::Abs(OptionSpeedGap - BaselineSpeedGap) <= ForceStatisticsTolerance);
	TestTrue(TEXT("GPU centroid motion is within 5% of the CPU centroid motion"), IsWithinRelativeTolerance(GpuCentroidMotion, CpuCentroidMotion, CentroidMotionTolerance));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsMixedBatchKeepsDisabledEntriesExactTest,
	"TDGame.FluidFXRhi.SolverOptions.MixedBatchKeepsDisabledEntriesExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsMixedBatchKeepsDisabledEntriesExactTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsRhiTestSupport;
	if (!CanRunSolverOptionsRhiTest(*this, TEXT("TDGame.FluidFXRhi.SolverOptions.MixedBatchKeepsDisabledEntriesExact")))
	{
		return false;
	}

	FTDFluidReadbackField MixedPlainField;
	FTDFluidReadbackField MixedOptionField;
	FTDFluidReadbackField SoloPlainField;
	FTDFluidReadbackField UnusedOptionField;
	if (!RunMixedBatchGpu(true, MixedPlainField, MixedOptionField) || !RunMixedBatchGpu(false, SoloPlainField, UnusedOptionField))
	{
		AddError(TEXT("The mixed batch GPU readback was not ready after the GPU idled or did not copy into a field"));
		return false;
	}

	TestTrue(TEXT("The plain entry tile holds density"), MeasureFieldStatistics(SoloPlainField).DensitySum > 0.0);
	TestFalse(TEXT("The option entry differs from the plain entry, so the option permutation ran"), AreFieldsBitIdentical(MixedOptionField, MixedPlainField));
	TestTrue(TEXT("The disabled entry in a mixed batch matches its solo run bit for bit"), AreFieldsBitIdentical(MixedPlainField, SoloPlainField));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverOptionsStageResolvesKindsTest,
	"TDGame.FluidFXRhi.SolverOptionsAsset.StageResolvesKinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverOptionsStageResolvesKindsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverOptionsRhiTestSupport;

	for (const ETDFluidPresetKind Kind : { ETDFluidPresetKind::SmokeKernelPressure, ETDFluidPresetKind::SmokeWindDrift, ETDFluidPresetKind::SmokeAnchoredCauldron })
	{
		const FString PresetPath = MakePresetObjectPath(Kind);
		const UTDFluidPresetAsset* Preset = LoadObject<UTDFluidPresetAsset>(nullptr, *PresetPath);
		if (!TestNotNull(*FString::Printf(TEXT("%s (editor_make_fluid_solver_options_stage.py)"), *PresetPath), Preset))
		{
			continue;
		}

		const FTDFluidSimulationConfig Expected = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		const FTDFluidSimulationConfig Actual = Preset->ResolveEffectiveConfig();
		TestTrue(*FString::Printf(TEXT("%s keeps its preset kind"), *PresetPath), Preset->PresetKind == Kind);
		TestTrue(*FString::Printf(TEXT("%s pressure method matches the JSON kind"), *PresetPath), Actual.PressureMethod == Expected.PressureMethod);
		TestTrue(*FString::Printf(TEXT("%s anchoring flag matches the JSON kind"), *PresetPath), Actual.bAnchorsDensityToActor == Expected.bAnchorsDensityToActor);
		TestTrue(*FString::Printf(TEXT("%s direct velocity matches the JSON kind"), *PresetPath), Actual.DirectVelocityCmPerSec.Equals(Expected.DirectVelocityCmPerSec));
	}

	if (!TestTrue(FString::Printf(TEXT("%s exists"), SolverOptionsStageMapPackage), FPackageName::DoesPackageExist(SolverOptionsStageMapPackage)))
	{
		return false;
	}

	const FString MapObjectPath = FString::Printf(TEXT("%s.%s"), SolverOptionsStageMapPackage, *FPackageName::GetShortName(SolverOptionsStageMapPackage));
	const UWorld* StageWorld = LoadObject<UWorld>(nullptr, *MapObjectPath);
	if (!TestNotNull(TEXT("solver options stage map loads"), StageWorld) || !TestNotNull(TEXT("solver options stage map has a level"), StageWorld->PersistentLevel.Get()))
	{
		return false;
	}

	int32 AnchoredActorCount = 0;
	for (const AActor* Actor : StageWorld->PersistentLevel->Actors)
	{
		if (!IsAnchoredStageActor(Actor))
		{
			continue;
		}

		++AnchoredActorCount;
		TestNotNull(*FString::Printf(TEXT("%s is attached to an orbit parent"), *Actor->GetName()), Actor->GetAttachParentActor());
	}
	TestTrue(TEXT("The stage places an anchored cauldron actor"), AnchoredActorCount > 0);
	return !HasAnyErrors();
}

#endif
