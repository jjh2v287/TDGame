#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuInputs.h"

namespace TDFluidGpuWrapAreaTestPrivate
{
	constexpr int32 ForcedMultigridSolverMode = 2;

	FTDFluidGpuSimulationSettings MakeWrappedSettings()
	{
		FTDFluidGpuSimulationSettings Settings;
		Settings.SimMode = ETDFluidGpuSimMode::FluidSim;
		Settings.Resolution = 512;
		Settings.PressureIterations = 12;
		Settings.PressureSolverModeRequest = ForcedMultigridSolverMode;
		Settings.AreaAddressModeRequest = TDFluidGpuSolverConstants::WrapAreaAddressModeRequest;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWrapAreaKeyAndSettingsTest,
	"TDGame.FluidFX.WrapArea.GpuKeyAndSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWrapAreaKeyAndSettingsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWrapAreaTestPrivate;
	const FTDFluidGpuSimulationSettings Wrapped = MakeWrappedSettings();
	const FTDFluidGpuBatchKey WrappedKey = FTDFluidGpuBatch::MakeKey(Wrapped);
	TestTrue(TEXT("Wrap request marks the batch key"), WrappedKey.bWrapsArea);
	TestEqual(TEXT("Wrap caps the resolution tier at 256"), WrappedKey.ResolutionTier, TDFluidGpuSolverConstants::WrapAreaMaximumResolution);
	TestTrue(TEXT("Wrap keeps the Jacobi solver"), WrappedKey.PressureSolver == ETDFluidGpuPressureSolver::Jacobi);
	TestEqual(TEXT("Wrap keeps the requested iterations"), WrappedKey.PressureIterations, 12);

	IConsoleVariable* SolverVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("TD.FluidFX.GpuPressureSolver"));
	if (SolverVariable)
	{
		const int32 PreviousValue = SolverVariable->GetInt();
		SolverVariable->Set(ForcedMultigridSolverMode, ECVF_SetByCode);
		const FTDFluidGpuBatchKey ForcedKey = FTDFluidGpuBatch::MakeKey(Wrapped);
		SolverVariable->Set(PreviousValue, ECVF_SetByCode);
		TestTrue(TEXT("Wrap ignores a forced multigrid console value"), ForcedKey.PressureSolver == ETDFluidGpuPressureSolver::Jacobi);
	}

	FTDFluidGpuSimulationSettings Clamped = Wrapped;
	Clamped.AreaAddressModeRequest = 0;
	TestFalse(TEXT("Clamp request keeps the default key"), FTDFluidGpuBatch::MakeKey(Clamped).bWrapsArea);

	FTDFluidGpuSimulationSettings WaterWrap = Wrapped;
	WaterWrap.bUsesWaterLayer = true;
	TestFalse(TEXT("Water layer never wraps"), FTDFluidGpuBatch::MakeKey(WaterWrap).bWrapsArea);

	const FTDFluidGpuActorParameters DefaultParameters;
	TestEqual(TEXT("Clamp flow lattice period stays zero"), DefaultParameters.FlowNoiseLatticePeriodPacked, 0u);
	TestEqual(TEXT("Clamp ambient lattice period stays zero"), DefaultParameters.AmbientNoiseLatticePeriodPacked, 0u);
	return true;
}

#endif
