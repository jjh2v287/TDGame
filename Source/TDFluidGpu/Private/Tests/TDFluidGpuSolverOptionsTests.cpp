#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuSolverOptionSettings.h"
#include "TDFluidGpuSolverOptions.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "TDFluidGpuPassTimestamps.h"
#include "TDFluidGpuStepPasses.h"

namespace TDFluidGpuSolverOptionsTestPrivate
{
	const TCHAR* const GpuPressureSolverVariableName = TEXT("TD.FluidFX.GpuPressureSolver");
	constexpr float StepDeltaTimeSeconds = 1.0f / 60.0f;
	constexpr int32 KernelTestResolution = 256;

	struct FTDFluidGpuSolverOptionsConsoleOverride
	{
		FTDFluidGpuSolverOptionsConsoleOverride(const TCHAR* VariableName, int32 OverrideValue)
			: Variable(IConsoleManager::Get().FindConsoleVariable(VariableName))
		{
			if (!Variable)
			{
				return;
			}

			PreviousValue = Variable->GetInt();
			Variable->Set(OverrideValue, ECVF_SetByCode);
			bIsApplied = Variable->GetInt() == OverrideValue;
		}

		~FTDFluidGpuSolverOptionsConsoleOverride()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 0;
		bool bIsApplied = false;
	};

	FTDFluidGpuSimulationSettings MakeSquareFluidSettings(int32 Resolution)
	{
		FTDFluidGpuSimulationSettings Settings;
		Settings.SimMode = ETDFluidGpuSimMode::FluidSim;
		Settings.Resolution = Resolution;
		Settings.ExtentCm = FVector2D(4000.0, 4000.0);
		return Settings;
	}

	FTDFluidGpuSimulationSettings MakeKernelRequestSettings(int32 Resolution, int32 PassesPerProjection)
	{
		FTDFluidGpuSimulationSettings Settings = MakeSquareFluidSettings(Resolution);
		Settings.PressureSolverModeRequest = TDFluidGpuSolverOptionConstants::KernelFeedbackPressureSolverModeRequest;
		Settings.SolverOptions.KernelPassesPerProjection = PassesPerProjection;
		return Settings;
	}

	FTDFluidGpuSimulationSettings MakeAllOptionsSettings()
	{
		FTDFluidGpuSimulationSettings Settings = MakeKernelRequestSettings(KernelTestResolution, 1);
		Settings.SolverOptions.FlowAmplitudeNoiseDampingPerSec = 2.0f;
		Settings.SolverOptions.AdvectionOffsetPlaneCmPerSec = FVector2D(60.0, 0.0);
		return Settings;
	}

	bool HasOptionFlag(const FTDFluidGpuSolverOptionParameters& Parameters, ETDFluidGpuSolverOptionFlags Flag)
	{
		return EnumHasAnyFlags(static_cast<ETDFluidGpuSolverOptionFlags>(Parameters.OptionFlags), Flag);
	}

	uint32 MakeExpectedForceFlags()
	{
		return static_cast<uint32>(ETDFluidGpuSolverOptionFlags::UsesFlowAmplitudeNoise | ETDFluidGpuSolverOptionFlags::UsesAdvectionOffset);
	}

	FTDFluidGpuActorInput MakeAmplitudeActor(uint64 ActorKey, const FIntPoint& LatticeOffsetCells, bool bWrapsArea)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = ActorKey;
		Actor.Settings.SimMode = ETDFluidGpuSimMode::FluidSim;
		Actor.Settings.Resolution = 128;
		Actor.Settings.ExtentCm = FVector2D(2000.0, 2000.0);
		Actor.Settings.FlowNoiseAccelerationCmPerSec2 = 300.0f;
		Actor.Settings.FlowNoiseScaleCm = 150.0f;
		Actor.Settings.SolverOptions.FlowAmplitudeNoiseDampingPerSec = 2.0f;
		Actor.Settings.SolverOptions.FlowAmplitudeNoiseScaleCm = 150.0f;
		Actor.Settings.SolverOptions.NoiseLatticeOffsetCells = LatticeOffsetCells;
		Actor.Settings.AreaAddressModeRequest = bWrapsArea ? TDFluidGpuSolverConstants::WrapAreaAddressModeRequest : static_cast<uint8>(0);
		Actor.DeltaTimeSeconds = StepDeltaTimeSeconds;
		Actor.WindowCenterPlaneCm = FVector2D(350.0, -120.0);
		return Actor;
	}

	bool PrepareSingleActor(const FTDFluidGpuActorInput& Actor, FTDFluidGpuActorParameters& OutActorParameters, FTDFluidGpuSolverOptionParameters& OutOptionParameters)
	{
		FTDFluidGpuFrameInputs FrameInputs;
		FrameInputs.Actors.Add(Actor);
		TArray<FTDFluidGpuBatch> Batches;
		FTDFluidGpuPreparedFrame PreparedFrame;
		FTDFluidGpuBatch::PrepareFrame(FrameInputs, Batches, PreparedFrame);
		for (const FTDFluidGpuBatch& Batch : Batches)
		{
			const FTDFluidGpuBatchFrame& Frame = Batch.GetFrame();
			if (Frame.Parameters.Num() != 1 || Frame.SolverOptionParameters.Num() != 1)
			{
				continue;
			}

			OutActorParameters = Frame.Parameters[0];
			OutOptionParameters = Frame.SolverOptionParameters[0];
			return true;
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuSolverOptionsKernelBatchKeyRulesTest,
	"TDGame.FluidFX.Gpu.SolverOptions.KernelBatchKeyRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuSolverOptionsKernelBatchKeyRulesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuSolverOptionsTestPrivate;

	const FTDFluidGpuBatchKey KernelKey = FTDFluidGpuBatch::MakeKey(MakeKernelRequestSettings(KernelTestResolution, 3));
	TestTrue(TEXT("Request 3 resolves to kernel feedback"), FTDFluidGpuBatch::ResolvePressureSolver(KernelTestResolution, TDFluidGpuSolverOptionConstants::KernelFeedbackPressureSolverModeRequest) == ETDFluidGpuPressureSolver::KernelFeedback);
	TestTrue(TEXT("A square clamped fluid actor with request 3 forms a kernel batch"), KernelKey.PressureSolver == ETDFluidGpuPressureSolver::KernelFeedback);
	TestEqual(TEXT("Kernel batch iterations carry the passes per projection"), KernelKey.PressureIterations, 3);

	FTDFluidGpuSimulationSettings NonSquare = MakeKernelRequestSettings(KernelTestResolution, 3);
	NonSquare.ExtentCm = FVector2D(4000.0, 3000.0);
	TestTrue(TEXT("A non square window falls back to Jacobi"), FTDFluidGpuBatch::MakeKey(NonSquare).PressureSolver == ETDFluidGpuPressureSolver::Jacobi);

	FTDFluidGpuSimulationSettings Wrapped = MakeKernelRequestSettings(KernelTestResolution, 3);
	Wrapped.AreaAddressModeRequest = TDFluidGpuSolverConstants::WrapAreaAddressModeRequest;
	TestTrue(TEXT("A wrapped window falls back to Jacobi"), FTDFluidGpuBatch::MakeKey(Wrapped).PressureSolver == ETDFluidGpuPressureSolver::Jacobi);

	FTDFluidGpuSimulationSettings Water = MakeKernelRequestSettings(KernelTestResolution, 3);
	Water.bUsesWaterLayer = true;
	FTDFluidGpuSimulationSettings WaterReference = Water;
	WaterReference.PressureSolverModeRequest = 0;
	TestTrue(TEXT("Water keeps its own pressure mode under request 3"), FTDFluidGpuBatch::MakeKey(Water) == FTDFluidGpuBatch::MakeKey(WaterReference));
	TestTrue(TEXT("Water never forms a kernel batch"), FTDFluidGpuBatch::MakeKey(Water).PressureSolver != ETDFluidGpuPressureSolver::KernelFeedback);

	const FTDFluidGpuBatchKey JacobiKey = FTDFluidGpuBatch::MakeKey(MakeSquareFluidSettings(KernelTestResolution));
	TestTrue(TEXT("The automatic solver stays Jacobi at 256"), JacobiKey.PressureSolver == ETDFluidGpuPressureSolver::Jacobi);
	TestFalse(TEXT("Kernel and Jacobi actors split into different batches"), JacobiKey == KernelKey);

	{
		FTDFluidGpuSolverOptionsConsoleOverride SolverOverride(GpuPressureSolverVariableName, TDFluidGpuSolverOptionConstants::KernelFeedbackPressureSolverModeRequest);
		TestTrue(TEXT("GPU pressure solver console variable accepts 3"), SolverOverride.bIsApplied);
		const FTDFluidGpuBatchKey ForcedKey = FTDFluidGpuBatch::MakeKey(MakeSquareFluidSettings(KernelTestResolution));
		TestTrue(TEXT("Console value 3 forces kernel feedback on a request 0 actor"), ForcedKey.PressureSolver == ETDFluidGpuPressureSolver::KernelFeedback);
		FTDFluidGpuSimulationSettings ForcedNonSquare = MakeSquareFluidSettings(KernelTestResolution);
		ForcedNonSquare.ExtentCm = FVector2D(4000.0, 3000.0);
		TestTrue(TEXT("Console value 3 still sends a non square actor to Jacobi"), FTDFluidGpuBatch::MakeKey(ForcedNonSquare).PressureSolver == ETDFluidGpuPressureSolver::Jacobi);
	}

	const FTDFluidGpuMemoryEstimate KernelEstimate = FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode::FluidSim, KernelTestResolution, false, false, TDFluidGpuSolverOptionConstants::KernelFeedbackPressureSolverModeRequest);
	const int64 ExpectedTransientBytes = static_cast<int64>(KernelTestResolution) * KernelTestResolution * TDFluidGpuSolverOptionConstants::KernelTransientBytesPerCell;
	TestEqual(TEXT("Kernel transient memory is 20 bytes per cell"), KernelEstimate.TransientBytes, ExpectedTransientBytes);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuSolverOptionsParameterLayoutAndFlagsTest,
	"TDGame.FluidFX.Gpu.SolverOptions.ParameterLayoutAndFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuSolverOptionsParameterLayoutAndFlagsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuSolverOptionsTestPrivate;

	TestEqual(TEXT("Entry parameters are 64 bytes"), static_cast<int32>(sizeof(FTDFluidGpuSolverOptionParameters)), 64);
	TestEqual(TEXT("OptionFlags offset"), static_cast<int32>(offsetof(FTDFluidGpuSolverOptionParameters, OptionFlags)), 0);
	TestEqual(TEXT("KernelRadiusTexels offset"), static_cast<int32>(offsetof(FTDFluidGpuSolverOptionParameters, KernelRadiusTexels)), 4);
	TestEqual(TEXT("KernelDivergenceRadiusTexels offset"), static_cast<int32>(offsetof(FTDFluidGpuSolverOptionParameters, KernelDivergenceRadiusTexels)), 8);
	TestEqual(TEXT("KernelFeedback offset"), static_cast<int32>(offsetof(FTDFluidGpuSolverOptionParameters, KernelFeedback)), 12);
	TestEqual(TEXT("KernelDivergenceScale offset"), static_cast<int32>(offsetof(FTDFluidGpuSolverOptionParameters, KernelDivergenceScale)), 16);
	TestEqual(TEXT("FlowAmplitudeDampingStep offset"), static_cast<int32>(offsetof(FTDFluidGpuSolverOptionParameters, FlowAmplitudeDampingStep)), 20);
	TestEqual(TEXT("AdvectionOffsetCmPerSec offset"), static_cast<int32>(offsetof(FTDFluidGpuSolverOptionParameters, AdvectionOffsetCmPerSec)), 24);
	TestEqual(TEXT("AmplitudeNoiseLatticeOrigin offset"), static_cast<int32>(offsetof(FTDFluidGpuSolverOptionParameters, AmplitudeNoiseLatticeOrigin)), 32);
	TestEqual(TEXT("AmplitudeNoiseLatticeOffset offset"), static_cast<int32>(offsetof(FTDFluidGpuSolverOptionParameters, AmplitudeNoiseLatticeOffset)), 40);
	TestEqual(TEXT("AmplitudeNoiseLatticeStepPerCell offset"), static_cast<int32>(offsetof(FTDFluidGpuSolverOptionParameters, AmplitudeNoiseLatticeStepPerCell)), 48);
	TestEqual(TEXT("AmplitudeNoiseLatticePeriodPacked offset"), static_cast<int32>(offsetof(FTDFluidGpuSolverOptionParameters, AmplitudeNoiseLatticePeriodPacked)), 56);
	TestEqual(TEXT("Padding offset"), static_cast<int32>(offsetof(FTDFluidGpuSolverOptionParameters, Padding)), 60);

	TestEqual(TEXT("Kernel weight at the center is 1"), ComputeTDFluidKernelWeight(0, 4), 1.0f, 1.0e-6f);
	TestEqual(TEXT("Kernel weight follows exp(-2 k^2 / R^2)"), ComputeTDFluidKernelWeight(2, 4), static_cast<float>(FMath::Exp(-0.5)), 1.0e-6f);
	TestEqual(TEXT("R4 divergence scale is 1.714"), ComputeTDFluidKernelDivergenceScale(4, 1.0f), 1.714f, 0.001f);
	TestEqual(TEXT("Divergence scale grows with the user scale"), ComputeTDFluidKernelDivergenceScale(4, 2.0f), 3.428f, 0.002f);

	const FTDFluidGpuSimulationSettings AllOptions = MakeAllOptionsSettings();
	const FTDFluidGpuSolverOptionParameters AllParameters = MakeTDFluidGpuSolverOptionParameters(AllOptions, FVector2D(100.0, -50.0), StepDeltaTimeSeconds, KernelTestResolution);
	TestEqual(TEXT("All options set the amplitude and offset flags"), AllParameters.OptionFlags, MakeExpectedForceFlags());
	TestFalse(TEXT("The entry never sets the kernel bit; the batch key decides"), HasOptionFlag(AllParameters, ETDFluidGpuSolverOptionFlags::UsesKernelPressure));
	TestEqual(TEXT("Kernel radius is copied"), static_cast<int32>(AllParameters.KernelRadiusTexels), AllOptions.SolverOptions.KernelRadiusTexels);
	TestEqual(TEXT("Kernel divergence radius is copied"), static_cast<int32>(AllParameters.KernelDivergenceRadiusTexels), AllOptions.SolverOptions.KernelDivergenceRadiusTexels);
	TestEqual(TEXT("Kernel feedback is copied"), AllParameters.KernelFeedback, AllOptions.SolverOptions.KernelFeedback, 1.0e-6f);
	TestEqual(TEXT("R4 entry divergence scale is 1.714"), AllParameters.KernelDivergenceScale, 1.714f, 0.001f);
	TestEqual(TEXT("Damping step is D times the step"), AllParameters.FlowAmplitudeDampingStep, AllOptions.SolverOptions.FlowAmplitudeNoiseDampingPerSec * StepDeltaTimeSeconds, 1.0e-6f);
	TestTrue(TEXT("Advection offset is copied"), AllParameters.AdvectionOffsetCmPerSec.Equals(FVector2f(60.0f, 0.0f), 1.0e-4f));

	const FTDFluidGpuSolverOptionParameters DefaultParameters = MakeTDFluidGpuSolverOptionParameters(MakeSquareFluidSettings(KernelTestResolution), FVector2D(100.0, -50.0), StepDeltaTimeSeconds, KernelTestResolution);
	TestEqual(TEXT("Default settings set no flags"), DefaultParameters.OptionFlags, 0u);

	FTDFluidGpuSimulationSettings Painter = AllOptions;
	Painter.SimMode = ETDFluidGpuSimMode::SimplePainter;
	TestEqual(TEXT("Painter entries set no flags"), MakeTDFluidGpuSolverOptionParameters(Painter, FVector2D::ZeroVector, StepDeltaTimeSeconds, KernelTestResolution).OptionFlags, 0u);

	FTDFluidGpuSimulationSettings Water = AllOptions;
	Water.bUsesWaterLayer = true;
	TestEqual(TEXT("Water entries set no flags"), MakeTDFluidGpuSolverOptionParameters(Water, FVector2D::ZeroVector, StepDeltaTimeSeconds, KernelTestResolution).OptionFlags, 0u);

	FTDFluidGpuSimulationSettings Wrapped = AllOptions;
	Wrapped.AreaAddressModeRequest = TDFluidGpuSolverConstants::WrapAreaAddressModeRequest;
	TestEqual(TEXT("Wrapped entries keep amplitude and offset"), MakeTDFluidGpuSolverOptionParameters(Wrapped, FVector2D::ZeroVector, StepDeltaTimeSeconds, KernelTestResolution).OptionFlags, MakeExpectedForceFlags());

	FTDFluidGpuSimulationSettings NonSquare = AllOptions;
	NonSquare.ExtentCm = FVector2D(4000.0, 3000.0);
	TestEqual(TEXT("Non square entries keep amplitude and offset"), MakeTDFluidGpuSolverOptionParameters(NonSquare, FVector2D::ZeroVector, StepDeltaTimeSeconds, KernelTestResolution).OptionFlags, MakeExpectedForceFlags());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuSolverOptionsAmplitudeLatticeMatchesFlowLatticeTest,
	"TDGame.FluidFX.Gpu.SolverOptions.AmplitudeLatticeMatchesFlowLattice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuSolverOptionsAmplitudeLatticeMatchesFlowLatticeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuSolverOptionsTestPrivate;

	const FIntPoint LatticeOffsetCells(37, 911);
	for (const bool bWrapsArea : { false, true })
	{
		const TCHAR* ModeName = bWrapsArea ? TEXT("Wrapped") : TEXT("Clamped");
		FTDFluidGpuActorParameters BaseActor;
		FTDFluidGpuActorParameters ShiftedActor;
		FTDFluidGpuSolverOptionParameters BaseOptions;
		FTDFluidGpuSolverOptionParameters ShiftedOptions;
		const bool bPreparedBase = PrepareSingleActor(MakeAmplitudeActor(31, FIntPoint::ZeroValue, bWrapsArea), BaseActor, BaseOptions);
		const bool bPreparedShifted = PrepareSingleActor(MakeAmplitudeActor(32, LatticeOffsetCells, bWrapsArea), ShiftedActor, ShiftedOptions);
		if (!TestTrue(FString::Printf(TEXT("%s: the frame carries one solver option entry per actor"), ModeName), bPreparedBase && bPreparedShifted))
		{
			continue;
		}

		TestTrue(FString::Printf(TEXT("%s: amplitude origin matches the flow origin"), ModeName), BaseOptions.AmplitudeNoiseLatticeOrigin == BaseActor.FlowNoiseLatticeOrigin);
		TestTrue(FString::Printf(TEXT("%s: amplitude fraction matches the flow fraction"), ModeName), BaseOptions.AmplitudeNoiseLatticeOffset.Equals(BaseActor.FlowNoiseLatticeOffset, 1.0e-6f));
		TestTrue(FString::Printf(TEXT("%s: amplitude step matches the flow step"), ModeName), BaseOptions.AmplitudeNoiseLatticeStepPerCell.Equals(BaseActor.FlowNoiseLatticeStepPerCell, 1.0e-6f));
		TestEqual(*FString::Printf(TEXT("%s: amplitude period matches the flow period"), ModeName), BaseOptions.AmplitudeNoiseLatticePeriodPacked, BaseActor.FlowNoiseLatticePeriodPacked);
		TestTrue(FString::Printf(TEXT("%s: the lattice offset moves the flow origin"), ModeName), ShiftedActor.FlowNoiseLatticeOrigin - BaseActor.FlowNoiseLatticeOrigin == LatticeOffsetCells);
		TestTrue(FString::Printf(TEXT("%s: the lattice offset moves the amplitude origin the same way"), ModeName), ShiftedOptions.AmplitudeNoiseLatticeOrigin - BaseOptions.AmplitudeNoiseLatticeOrigin == LatticeOffsetCells);
		TestTrue(FString::Printf(TEXT("%s: shifted amplitude origin still matches the shifted flow origin"), ModeName), ShiftedOptions.AmplitudeNoiseLatticeOrigin == ShiftedActor.FlowNoiseLatticeOrigin);
	}
	return true;
}

namespace TDFluidGpuSolverOptionsCostTestPrivate
{
	constexpr int32 CostResolution = 512;
	constexpr int32 CostWarmupFrameCount = 8;
	constexpr int32 CostMeasuredFrameCount = 64;
	constexpr int32 ForcedJacobiSolverMode = 1;
	constexpr int32 AutomaticSolverMode = 0;
	const TCHAR* const CostCsvPath = TEXT("C:/Project/TDGame/Saved/AgentOps/20261029/wave3/fx15a-cost.csv");

	struct FTDFluidSolverOptionsCostCase
	{
		const TCHAR* Name = TEXT("");
		int32 SolverMode = 0;
		int32 PressureIterations = 6;
		int32 KernelRadiusTexels = 4;
		int32 KernelPassesPerProjection = 1;
		bool bUsesForceOptions = false;
	};

	struct FTDFluidSolverOptionsCostSample
	{
		TMap<FString, double> TotalMillisecondsByLabel;
		int32 MeasuredFrameCount = 0;
	};

	FTDFluidGpuActorInput MakeCostActor(const FTDFluidSolverOptionsCostCase& CostCase)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = 1515;
		Actor.Settings = TDFluidGpuSolverOptionsTestPrivate::MakeSquareFluidSettings(CostResolution);
		Actor.Settings.PressureIterations = CostCase.PressureIterations;
		Actor.Settings.SolverOptions.KernelRadiusTexels = CostCase.KernelRadiusTexels;
		Actor.Settings.SolverOptions.KernelPassesPerProjection = CostCase.KernelPassesPerProjection;
		if (CostCase.bUsesForceOptions)
		{
			Actor.Settings.SolverOptions.FlowAmplitudeNoiseDampingPerSec = 2.0f;
			Actor.Settings.SolverOptions.AdvectionOffsetPlaneCmPerSec = FVector2D(60.0, 0.0);
		}
		Actor.DeltaTimeSeconds = TDFluidGpuSolverOptionsTestPrivate::StepDeltaTimeSeconds;
		return Actor;
	}

	FTDFluidSolverOptionsCostSample MeasureCostCase(const FTDFluidSolverOptionsCostCase& CostCase)
	{
		TSharedRef<FTDFluidSolverOptionsCostSample, ESPMode::ThreadSafe> Sample = MakeShared<FTDFluidSolverOptionsCostSample, ESPMode::ThreadSafe>();
		const FTDFluidGpuActorInput Actor = MakeCostActor(CostCase);
		ENQUEUE_RENDER_COMMAND(TDFluidSolverOptionsCostFrames)(
			[Sample, Actor](FRHICommandListImmediate& RHICmdList)
			{
				TArray<FTDFluidGpuBatch> Batches;
				TArray<FTDFluidGpuPassTimestamps> FrameTimestamps;
				for (int32 FrameIndex = 0; FrameIndex < CostWarmupFrameCount + CostMeasuredFrameCount; ++FrameIndex)
				{
					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex);
					FrameInputs.Actors.Add(Actor);
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidSolverOptionsCost"));
					if (FrameIndex < CostWarmupFrameCount)
					{
						AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, Batches);
					}
					else
					{
						AddTDFluidGpuTimestampedFramePasses(GraphBuilder, FrameInputs, Batches, FrameTimestamps.AddDefaulted_GetRef());
					}
					GraphBuilder.Execute();
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				}

				RHICmdList.SubmitAndBlockUntilGPUIdle();
				for (const FTDFluidGpuPassTimestamps& Timestamps : FrameTimestamps)
				{
					uint64 PreviousMicroseconds = 0;
					bool bHasPreviousMicroseconds = false;
					for (int32 QueryIndex = 0; QueryIndex < Timestamps.Queries.Num(); ++QueryIndex)
					{
						uint64 Microseconds = 0;
						if (!RHIGetRenderQueryResult(Timestamps.Queries[QueryIndex], Microseconds, true))
						{
							bHasPreviousMicroseconds = false;
							continue;
						}
						if (bHasPreviousMicroseconds && Timestamps.Labels.IsValidIndex(QueryIndex) && Microseconds >= PreviousMicroseconds)
						{
							Sample->TotalMillisecondsByLabel.FindOrAdd(Timestamps.Labels[QueryIndex]) += static_cast<double>(Microseconds - PreviousMicroseconds) / 1000.0;
						}
						PreviousMicroseconds = Microseconds;
						bHasPreviousMicroseconds = true;
					}
				}
				Sample->MeasuredFrameCount = FrameTimestamps.Num();
				Batches.Empty();
			});
		FlushRenderingCommands();
		return *Sample;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuSolverOptionsCost512Test,
	"TDGame.FluidFXRhi.SolverOptionsCost.Cost512",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuSolverOptionsCost512Test::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuSolverOptionsTestPrivate;
	using namespace TDFluidGpuSolverOptionsCostTestPrivate;

	if (GUsingNullRHI || !FApp::CanEverRender())
	{
		AddError(TEXT("TDGame.FluidFXRhi.SolverOptionsCost.Cost512 needs a rendering RHI but none is active. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."));
		return false;
	}

	const int32 KernelSolverMode = TDFluidGpuSolverOptionConstants::KernelFeedbackPressureSolverModeRequest;
	const FTDFluidSolverOptionsCostCase Cases[] = {
		{ TEXT("jacobi6"), ForcedJacobiSolverMode, 6, 4, 1, false },
		{ TEXT("multigrid_auto"), AutomaticSolverMode, 6, 4, 1, false },
		{ TEXT("kernel_r4_n1"), KernelSolverMode, 6, 4, 1, false },
		{ TEXT("kernel_r7_n2"), KernelSolverMode, 6, 7, 2, false },
		{ TEXT("kernel_r4_n1_force_options"), KernelSolverMode, 6, 4, 1, true },
	};

	FString Csv = TEXT("case,label,mean_ms_per_frame,frames\n");
	bool bIsEveryOverrideApplied = true;
	for (const FTDFluidSolverOptionsCostCase& CostCase : Cases)
	{
		FTDFluidGpuSolverOptionsConsoleOverride SolverOverride(GpuPressureSolverVariableName, CostCase.SolverMode);
		bIsEveryOverrideApplied &= SolverOverride.bIsApplied;
		const FTDFluidSolverOptionsCostSample Sample = MeasureCostCase(CostCase);
		TestTrue(FString::Printf(TEXT("%s measured timestamped frames"), CostCase.Name), Sample.MeasuredFrameCount > 0);
		for (const TPair<FString, double>& LabelTotal : Sample.TotalMillisecondsByLabel)
		{
			const double MeanMilliseconds = Sample.MeasuredFrameCount > 0 ? LabelTotal.Value / Sample.MeasuredFrameCount : 0.0;
			Csv += FString::Printf(TEXT("%s,%s,%.4f,%d\n"), CostCase.Name, *LabelTotal.Key, MeanMilliseconds, Sample.MeasuredFrameCount);
		}
	}
	TestTrue(TEXT("Every GPU pressure solver override applied"), bIsEveryOverrideApplied);
	AddInfo(Csv);
	TestTrue(TEXT("Saved the solver option cost table"), FFileHelper::SaveStringToFile(Csv, CostCsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	return !HasAnyErrors();
}

#endif
