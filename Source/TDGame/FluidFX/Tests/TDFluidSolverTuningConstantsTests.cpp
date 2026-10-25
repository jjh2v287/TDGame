#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"
#include "TDFluidGpuBatch.h"

namespace TDFluidSolverTuningConstantsTestPrivate
{
	constexpr float CappedCellSpeedCmPerSec = 600.0f;
	constexpr float RequestedRadialDriftCmPerSec = 1000.0f;
	constexpr float RequestedPunctureCmPerSec = 900.0f;
	constexpr float StampSpeedCmPerSec = 5000.0f;
	constexpr float CellSpeedToleranceCmPerSec = 0.01f;
	constexpr int32 TestResolution = 64;

	FTDFluidSimulationConfig MakeCappedConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.GridResolution = TestResolution;
		Config.SimExtentCm = FVector2D(2000.0, 2000.0);
		Config.MaxCellSpeedCmPerSec = CappedCellSpeedCmPerSec;
		Config.VortexRadialDriftCmPerSec = RequestedRadialDriftCmPerSec;
		Config.ActorBrushPunctureCmPerSec = RequestedPunctureCmPerSec;
		return Config;
	}

	FTDFluidBrushStamp MakeFastStamp()
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector::ZeroVector;
		Stamp.WorldVelocity = FVector(StampSpeedCmPerSec, 0.0, 0.0);
		Stamp.RadiusCm = 300.0f;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 1.0f;
		return Stamp;
	}

	float FindLargestCpuCellSpeed(const FTDFluidGrid2DSolver& Solver)
	{
		float LargestSpeed = 0.0f;
		for (int32 CellY = 0; CellY < Solver.GetResolution(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Solver.GetResolution(); ++CellX)
			{
				LargestSpeed = FMath::Max(LargestSpeed, static_cast<float>(Solver.SampleAtCell(CellX, CellY).VelocityCmPerSec.Size()));
			}
		}
		return LargestSpeed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSolverTuningConstantsReachBothBackendsTest,
	"TDGame.FluidFX.SolverTuning.ConstantsReachBothBackends",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSolverTuningConstantsReachBothBackendsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSolverTuningConstantsTestPrivate;

	const FTDFluidSimulationConfig DefaultConfig;
	TestEqual(TEXT("Default cell speed limit keeps the old constant"), DefaultConfig.MaxCellSpeedCmPerSec, TDFluidGpuSolverConstants::MaxCellSpeedCmPerSec);
	TestEqual(TEXT("Default brush velocity response keeps the old constant"), DefaultConfig.BrushVelocityResponsePerSec, TDFluidGpuSolverConstants::BrushVelocityResponsePerSec);
	TestEqual(TEXT("Default brush displacement ratio keeps the old constant"), DefaultConfig.BrushDisplacementRatio, TDFluidGpuSolverConstants::BrushDisplacementRatio);
	TestEqual(TEXT("Default brush erase rate keeps the old constant"), DefaultConfig.BrushEraseRatePerSec, TDFluidGpuSolverConstants::BrushEraseRatePerSec);

	const FTDFluidGpuSimulationSettings DefaultSettings = FTDFluidGpuSolverProxy::MakeSimulationSettings(DefaultConfig, TestResolution);
	const FTDFluidGpuSimulationSettings ConstructedSettings;
	TestEqual(TEXT("Default GPU settings carry the cell speed limit"), DefaultSettings.MaxCellSpeedCmPerSec, ConstructedSettings.MaxCellSpeedCmPerSec);
	TestEqual(TEXT("Default GPU settings carry the brush velocity response"), DefaultSettings.BrushVelocityResponsePerSec, ConstructedSettings.BrushVelocityResponsePerSec);
	TestEqual(TEXT("Default GPU settings carry the brush displacement ratio"), DefaultSettings.BrushDisplacementRatio, ConstructedSettings.BrushDisplacementRatio);
	TestEqual(TEXT("Default GPU settings carry the brush erase rate"), DefaultSettings.BrushEraseRatePerSec, ConstructedSettings.BrushEraseRatePerSec);
	TestEqual(TEXT("Default GPU settings carry the pressure solver request"), static_cast<int32>(DefaultSettings.PressureSolverModeRequest), static_cast<int32>(ConstructedSettings.PressureSolverModeRequest));
	TestEqual(TEXT("Default GPU settings carry the multigrid cycle request"), DefaultSettings.MultigridCyclesRequest, ConstructedSettings.MultigridCyclesRequest);
	TestEqual(TEXT("Default GPU settings carry the water kernel radius"), DefaultSettings.WaterKernelRadius, ConstructedSettings.WaterKernelRadius);
	TestEqual(TEXT("Default GPU settings carry the water kernel feedback"), DefaultSettings.WaterKernelFeedback, ConstructedSettings.WaterKernelFeedback);
	TestEqual(TEXT("Default GPU settings carry the water kernel scale"), DefaultSettings.WaterKernelScale, ConstructedSettings.WaterKernelScale);
	TestEqual(TEXT("Default GPU settings carry the water multigrid cycles"), DefaultSettings.WaterMultigridCycles, ConstructedSettings.WaterMultigridCycles);

	const FTDFluidSimulationConfig CappedConfig = MakeCappedConfig();
	FTDFluidGrid2DSolver CpuSolver;
	CpuSolver.Initialize(CappedConfig, FVector2D::ZeroVector);
	TestTrue(TEXT("CPU solver initializes"), CpuSolver.IsInitialized());
	TestEqual(TEXT("CPU radial drift is clamped by the field"), CpuSolver.GetConfig().VortexRadialDriftCmPerSec, CappedCellSpeedCmPerSec);
	TestEqual(TEXT("CPU puncture speed is clamped by the field"), CpuSolver.GetConfig().ActorBrushPunctureCmPerSec, CappedCellSpeedCmPerSec);

	TArray<FTDFluidBrushStamp> CpuStamps;
	CpuStamps.Add(MakeFastStamp());
	CpuSolver.ApplyBrushStamps(CpuStamps, 1.0f);
	const float LargestCpuSpeed = FindLargestCpuCellSpeed(CpuSolver);
	TestTrue(TEXT("CPU stamp velocity moves the cells"), LargestCpuSpeed > 0.5f * CappedCellSpeedCmPerSec);
	TestTrue(TEXT("CPU stamp velocity stays under the field limit"), LargestCpuSpeed <= CappedCellSpeedCmPerSec + CellSpeedToleranceCmPerSec);

	FTDFluidGpuFrameInputs FrameInputs;
	FTDFluidGpuActorInput& GpuActor = FrameInputs.Actors.AddDefaulted_GetRef();
	GpuActor.ActorKey = 1;
	GpuActor.Settings = FTDFluidGpuSolverProxy::MakeSimulationSettings(CappedConfig, TestResolution);
	GpuActor.DeltaTimeSeconds = 1.0f / 60.0f;
	GpuActor.bShouldResetState = true;
	GpuActor.Stamps.Add(FTDFluidGpuSolverProxy::MakeStampInput(MakeFastStamp()));

	TArray<FTDFluidGpuBatch> Batches;
	FTDFluidGpuPreparedFrame PreparedFrame;
	FTDFluidGpuBatch::PrepareFrame(FrameInputs, Batches, PreparedFrame);
	if (!TestEqual(TEXT("GPU frame forms one batch"), Batches.Num(), 1) || !TestEqual(TEXT("GPU batch holds one actor"), Batches[0].GetFrame().Parameters.Num(), 1))
	{
		return false;
	}

	const FTDFluidGpuActorParameters& GpuParameters = Batches[0].GetFrame().Parameters[0];
	TestEqual(TEXT("GPU parameters carry the field limit"), GpuParameters.MaxCellSpeedCmPerSec, CappedCellSpeedCmPerSec);
	TestEqual(TEXT("GPU radial drift matches the CPU clamp"), GpuParameters.VortexRadialDriftCmPerSec, CpuSolver.GetConfig().VortexRadialDriftCmPerSec);
	TestEqual(TEXT("GPU parameters carry the displacement ratio"), GpuParameters.BrushDisplacementRatio, CappedConfig.BrushDisplacementRatio);
	if (!TestEqual(TEXT("GPU frame packs one stamp"), PreparedFrame.StampBins.Stamps.Num(), 1))
	{
		return false;
	}

	const float PackedSpeed = PreparedFrame.StampBins.Stamps[0].TargetVelocityCmPerSec.Size();
	TestTrue(TEXT("GPU packed stamp velocity reaches the field limit"), PackedSpeed > CappedCellSpeedCmPerSec - 1.0f);
	TestTrue(TEXT("GPU packed stamp velocity stays under the field limit"), PackedSpeed <= CappedCellSpeedCmPerSec + CellSpeedToleranceCmPerSec);
	return !HasAnyErrors();
}

#endif
