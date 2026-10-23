#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/IConsoleManager.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "TDFluidGpuBackendSelection.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidGpuWaterTests, Log, All);

namespace TDFluidGpuWaterTestPrivate
{
	constexpr float HeadlessWaterStepSeconds = 1.0f / 60.0f;
	constexpr int32 FallbackStepCount = 60;
	constexpr int32 FallbackGridResolution = 64;
	constexpr float FallbackCellSizeCm = 18.75f;
	const TCHAR* const WaterGpuVariableName = TEXT("TD.FluidFX.WaterGpu");
	const TCHAR* const SolverBackendVariableName = TEXT("TD.FluidFX.SolverBackend");
	const FVector FallbackAnchorLocation(240.0, -90.0, 0.0);

	struct FTDWaterGpuConsoleVariableScope
	{
		FTDWaterGpuConsoleVariableScope(const TCHAR* Name, int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (!Variable)
			{
				return;
			}

			PreviousValue = Variable->GetString();
			Variable->Set(Value, ECVF_SetByCode);
		}

		~FTDWaterGpuConsoleVariableScope()
		{
			if (Variable)
			{
				Variable->Set(*PreviousValue, ECVF_SetByCode);
			}
		}

		FTDWaterGpuConsoleVariableScope(const FTDWaterGpuConsoleVariableScope&) = delete;
		FTDWaterGpuConsoleVariableScope& operator=(const FTDWaterGpuConsoleVariableScope&) = delete;

		bool IsValid() const
		{
			return Variable != nullptr;
		}

		IConsoleVariable* Variable = nullptr;
		FString PreviousValue;
	};

	FString MakeMissingWaterGpuMessage(const FString& Label)
	{
		return FString::Printf(TEXT("%s: TD.FluidFX.WaterGpu is not registered, so the water layer still forces the CPU solver (FX-4b not implemented)"), *Label);
	}

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	struct FTDFluidGpuWaterHeadlessWorld
	{
		FTDFluidGpuWaterHeadlessWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidGpuWaterHeadlessWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidGpuWaterHeadlessWorld()
		{
			if (!World)
			{
				return;
			}

			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			FlushRenderingCommands();
		}

		FTDFluidGpuWaterHeadlessWorld(const FTDFluidGpuWaterHeadlessWorld&) = delete;
		FTDFluidGpuWaterHeadlessWorld& operator=(const FTDFluidGpuWaterHeadlessWorld&) = delete;

		void StepWorld(float DeltaSeconds)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	FTDFluidSimulationConfig MakeFallbackWaterConfig(ETDFluidSolverBackend Backend)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = FallbackGridResolution;
		Config.SimExtentCm = FVector2D(FallbackGridResolution * FallbackCellSizeCm, FallbackGridResolution * FallbackCellSizeCm);
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = 0.35f;
		Config.PressureIterations = 12;
		Config.VorticityStrength = 0.3f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 1.0f;
		Config.ActorBrushDensityStrength = 0.6f;
		Config.MaxPointsPerTick = 512;
		Config.bEnableWaterLayer = true;
		Config.WhitewaterFromVorticity = 0.15f;
		Config.WhitewaterVorticityThresholdPerSec = 2.0f;
		Config.RippleWaveSpeedCmPerSec = 120.0f;
		Config.RippleDampingPerSec = 1.2f;
		Config.RippleFlowAdvection = 1.0f;
		Config.SolverBackend = Backend;
		return Config;
	}

	void AppendFallbackStamps(int32 StepIndex, TArray<FTDFluidBrushStamp>& OutStamps)
	{
		for (int32 StampIndex = 0; StampIndex < 2; ++StampIndex)
		{
			const double Angle = StepIndex * 0.09 + StampIndex * UE_DOUBLE_PI;
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FallbackAnchorLocation + FVector(FMath::Cos(Angle) * 300.0, FMath::Sin(Angle) * 90.0, 0.0);
			Stamp.WorldVelocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0) * 400.0;
			Stamp.RadiusCm = 70.0f;
			Stamp.DensityStrength = 0.6f;
			Stamp.VelocityStrength = 1.0f;
			Stamp.RippleImpulseCm = 2.5f;
			Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
			OutStamps.Add(Stamp);
		}
	}

	struct FTDFluidWaterFallbackRun
	{
		bool bIsValid = false;
		bool bUsedGpu = false;
		int32 Resolution = 0;
		uint32 StateChecksum = 0;
		uint32 WaterChecksum = 0;
		TArray<FFloat16Color> DensityPressurePixels;
		TArray<FFloat16Color> VelocityPixels;
		TArray<FFloat16Color> WaterSurfacePixels;
	};

	FTDFluidWaterFallbackRun RunFallbackScenario(FAutomationTestBase& Test, ETDFluidSolverBackend Backend)
	{
		FTDFluidWaterFallbackRun Run;
		FTDFluidGpuWaterHeadlessWorld HeadlessWorld;
		const FTransform SpawnTransform(FRotator::ZeroRotator, FallbackAnchorLocation);
		ATDFluidLiveActor* LiveActor = HeadlessWorld.World->SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!Test.TestNotNull(TEXT("Fallback scenario spawned a water live actor"), LiveActor) || !LiveActor->LiveComponent)
		{
			return Run;
		}

		LiveActor->LiveComponent->SimulationConfig = MakeFallbackWaterConfig(Backend);
		LiveActor->FinishSpawning(SpawnTransform);

		TArray<FTDFluidBrushStamp> StepStamps;
		for (int32 StepIndex = 0; StepIndex < FallbackStepCount; ++StepIndex)
		{
			StepStamps.Reset();
			AppendFallbackStamps(StepIndex, StepStamps);
			for (const FTDFluidBrushStamp& Stamp : StepStamps)
			{
				LiveActor->LiveComponent->QueueBrushStamp(Stamp);
			}
			HeadlessWorld.StepWorld(HeadlessWaterStepSeconds);
		}

		const UTDFluidLiveComponent& Component = *LiveActor->LiveComponent;
		Run.bUsedGpu = Component.IsUsingGpuSolver();
		Run.Resolution = Component.GetSimulationResolution();
		Run.bIsValid = Component.GetSolver().IsInitialized() && !Component.GetPackedDensityPressurePixels().IsEmpty() && !Component.GetPackedWaterSurfacePixels().IsEmpty();
		Run.StateChecksum = Component.GetSolver().ComputeStateChecksum();
		Run.WaterChecksum = Component.GetSolver().ComputeWaterStateChecksum();
		Run.DensityPressurePixels = Component.GetPackedDensityPressurePixels();
		Run.VelocityPixels = Component.GetPackedVelocityPixels();
		Run.WaterSurfacePixels = Component.GetPackedWaterSurfacePixels();
		return Run;
	}

	bool ArePixelsBitIdentical(const TArray<FFloat16Color>& Expected, const TArray<FFloat16Color>& Actual)
	{
		return Expected.Num() == Actual.Num() && FMemory::Memcmp(Expected.GetData(), Actual.GetData(), Expected.Num() * sizeof(FFloat16Color)) == 0;
	}

	struct FTDFluidWaterDecisionCase
	{
		const TCHAR* Name;
		ETDFluidGpuSolverPreference Preference;
		int32 RequestedResolution;
		bool bIsWaterLayerActive;
		bool bHasRenderingRhi;
		bool bSupportsShaderModel5;
		ETDFluidGpuBackendOverride Override;
		bool bExpectsGpu;
		int32 ExpectedResolution;
	};

	void ExpectDecisionCases(FAutomationTestBase& Test, const TCHAR* RangeLabel, const FTDFluidGpuBackendEnvironment& CapturedEnvironment, TConstArrayView<FTDFluidWaterDecisionCase> DecisionCases)
	{
		for (const FTDFluidWaterDecisionCase& DecisionCase : DecisionCases)
		{
			FTDFluidGpuBackendEnvironment CaseEnvironment = CapturedEnvironment;
			CaseEnvironment.bHasRenderingRhi = DecisionCase.bHasRenderingRhi;
			CaseEnvironment.bSupportsShaderModel5 = DecisionCase.bSupportsShaderModel5;
			CaseEnvironment.Override = DecisionCase.Override;

			FTDFluidGpuBackendQuery Query;
			Query.Preference = DecisionCase.Preference;
			Query.RequestedResolution = DecisionCase.RequestedResolution;
			Query.bIsWaterLayerActive = DecisionCase.bIsWaterLayerActive;
			const FTDFluidGpuBackendDecision Decision = FTDFluidGpuBackendSelection::Decide(Query, CaseEnvironment);
			Test.TestTrue(*FString::Printf(TEXT("%s %s: backend"), RangeLabel, DecisionCase.Name), Decision.bUsesGpu == DecisionCase.bExpectsGpu);
			Test.TestEqual(*FString::Printf(TEXT("%s %s: resolution"), RangeLabel, DecisionCase.Name), static_cast<int32>(Decision.Resolution), static_cast<int32>(DecisionCase.ExpectedResolution));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterHeadlessBackendSelectsGpuForWaterLayerTest,
	"TDGame.FluidFX.Water.Gpu.BackendSelectsGpuForWaterLayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterHeadlessBackendSelectsGpuForWaterLayerTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFX.Water.Gpu.BackendSelectsGpuForWaterLayer");
	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	TUniquePtr<FTDWaterGpuConsoleVariableScope> EnabledWaterGpuScope = MakeUnique<FTDWaterGpuConsoleVariableScope>(WaterGpuVariableName, 1);
	if (!EnabledWaterGpuScope->IsValid())
	{
		AddError(MakeMissingWaterGpuMessage(CaseLabel));
		return false;
	}

	constexpr ETDFluidGpuSolverPreference GpuPreference = ETDFluidGpuSolverPreference::Gpu;
	constexpr ETDFluidGpuSolverPreference CpuPreference = ETDFluidGpuSolverPreference::Cpu;
	constexpr ETDFluidGpuSolverPreference AutoPreference = ETDFluidGpuSolverPreference::Auto;
	constexpr ETDFluidGpuBackendOverride FollowPresetOverride = ETDFluidGpuBackendOverride::FollowPreset;
	constexpr ETDFluidGpuBackendOverride ForceGpuOverride = ETDFluidGpuBackendOverride::ForceGpuWhenPossible;
	constexpr ETDFluidGpuBackendOverride ForceCpuOverride = ETDFluidGpuBackendOverride::ForceCpu;

	const FTDFluidWaterDecisionCase EnabledCases[] = {
		{ TEXT("Water Gpu 192 selects the GPU water tier 192"), GpuPreference, 192, true, true, true, FollowPresetOverride, true, 192 },
		{ TEXT("Water Gpu 100 selects the GPU water tier 128"), GpuPreference, 100, true, true, true, FollowPresetOverride, true, 128 },
		{ TEXT("Water Gpu 200 selects the GPU water tier 256"), GpuPreference, 200, true, true, true, FollowPresetOverride, true, 256 },
		{ TEXT("Water Gpu 300 selects the GPU water tier 512"), GpuPreference, 300, true, true, true, FollowPresetOverride, true, 512 },
		{ TEXT("Water Auto 192 stays on CPU 192"), AutoPreference, 192, true, true, true, FollowPresetOverride, false, 192 },
		{ TEXT("Water Auto 300 selects GPU 512"), AutoPreference, 300, true, true, true, FollowPresetOverride, true, 512 },
		{ TEXT("Water Cpu 192 with console force GPU selects the GPU water tier 192"), CpuPreference, 192, true, true, true, ForceGpuOverride, true, 192 },
		{ TEXT("Water Gpu 192 with console force CPU stays on CPU 192"), GpuPreference, 192, true, true, true, ForceCpuOverride, false, 192 },
		{ TEXT("Water Gpu 192 without a rendering RHI stays on CPU 192"), GpuPreference, 192, true, false, false, FollowPresetOverride, false, 192 },
		{ TEXT("Water Gpu 192 below SM5 stays on CPU 192"), GpuPreference, 192, true, true, false, FollowPresetOverride, false, 192 },
		{ TEXT("Water tier 40 rounds up to 64"), GpuPreference, 40, true, true, true, FollowPresetOverride, true, 64 },
		{ TEXT("Water tier 100 rounds up to 128"), GpuPreference, 100, true, true, true, FollowPresetOverride, true, 128 },
		{ TEXT("Water tier 129 rounds up to 192"), GpuPreference, 129, true, true, true, FollowPresetOverride, true, 192 },
		{ TEXT("Water tier 192 keeps 192"), GpuPreference, 192, true, true, true, FollowPresetOverride, true, 192 },
		{ TEXT("Water tier 200 rounds up to 256"), GpuPreference, 200, true, true, true, FollowPresetOverride, true, 256 },
		{ TEXT("Water tier 300 rounds up to 512"), GpuPreference, 300, true, true, true, FollowPresetOverride, true, 512 },
		{ TEXT("Water tier 1000 rounds up to 1024"), GpuPreference, 1000, true, true, true, FollowPresetOverride, true, 1024 },
		{ TEXT("Water tier 4096 clamps to 2048"), GpuPreference, 4096, true, true, true, FollowPresetOverride, true, 2048 },
		{ TEXT("Nonwater Gpu 192 keeps the power of two tier 256"), GpuPreference, 192, false, true, true, FollowPresetOverride, true, 256 },
	};

	FTDFluidGpuBackendEnvironment EnabledEnvironment = FTDFluidGpuBackendSelection::CaptureEnvironment();
	EnabledEnvironment.bHasRenderingRhi = true;
	EnabledEnvironment.bSupportsShaderModel5 = true;
	ExpectDecisionCases(*this, TEXT("TD.FluidFX.WaterGpu 1:"), EnabledEnvironment, MakeArrayView(EnabledCases));
	EnabledWaterGpuScope.Reset();

	const FTDFluidWaterDecisionCase DisabledCases[] = {
		{ TEXT("Water Gpu 192 stays on CPU 192"), GpuPreference, 192, true, true, true, FollowPresetOverride, false, 192 },
		{ TEXT("Water Gpu 192 without a rendering RHI stays on CPU 192"), GpuPreference, 192, true, false, false, FollowPresetOverride, false, 192 },
	};

	FTDWaterGpuConsoleVariableScope DisabledWaterGpuScope(WaterGpuVariableName, 0);
	FTDFluidGpuBackendEnvironment DisabledEnvironment = FTDFluidGpuBackendSelection::CaptureEnvironment();
	DisabledEnvironment.bHasRenderingRhi = true;
	DisabledEnvironment.bSupportsShaderModel5 = true;
	ExpectDecisionCases(*this, TEXT("TD.FluidFX.WaterGpu 0:"), DisabledEnvironment, MakeArrayView(DisabledCases));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterHeadlessFallsBackToCpuWithoutRhiTest,
	"TDGame.FluidFX.Water.Gpu.FallsBackToCpuWithoutRhi",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterHeadlessFallsBackToCpuWithoutRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterTestPrivate;

	const bool bHasActiveRenderingRhi = HasRenderingRhi();
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	if (!WaterGpuScope.IsValid())
	{
		AddInfo(TEXT("TD.FluidFX.WaterGpu is not registered: the fallback runs with the water layer forcing the CPU solver."));
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, bHasActiveRenderingRhi ? 2 : 0);
	if (bHasActiveRenderingRhi && !SolverBackendScope.IsValid())
	{
		AddError(TEXT("TD.FluidFX.SolverBackend is not registered, so the CPU fallback cannot be forced under a rendering RHI."));
		return false;
	}
	AddInfo(bHasActiveRenderingRhi
		? TEXT("A rendering RHI is active: the CPU fallback is forced through TD.FluidFX.SolverBackend=2.")
		: TEXT("The null RHI is active: the CPU fallback comes from the environment."));

	const FTDFluidWaterFallbackRun GpuPreferenceRun = RunFallbackScenario(*this, ETDFluidSolverBackend::Gpu);
	const FTDFluidWaterFallbackRun CpuPreferenceRun = RunFallbackScenario(*this, ETDFluidSolverBackend::Cpu);
	const FString FallbackLine = FString::Printf(TEXT("TDFluidGpuWaterFallback: gpu_pref state=0x%08X water=0x%08X cpu_pref state=0x%08X water=0x%08X"), GpuPreferenceRun.StateChecksum, GpuPreferenceRun.WaterChecksum, CpuPreferenceRun.StateChecksum, CpuPreferenceRun.WaterChecksum);
	UE_LOG(LogTDFluidGpuWaterTests, Display, TEXT("%s"), *FallbackLine);
	AddInfo(FallbackLine);

	TestTrue(TEXT("The Gpu preference water run produced CPU state and pixels"), GpuPreferenceRun.bIsValid);
	TestTrue(TEXT("The Cpu preference water run produced CPU state and pixels"), CpuPreferenceRun.bIsValid);
	TestFalse(TEXT("The Gpu preference water actor falls back to the CPU solver"), GpuPreferenceRun.bUsedGpu);
	TestFalse(TEXT("The Cpu preference water actor uses the CPU solver"), CpuPreferenceRun.bUsedGpu);
	TestEqual(TEXT("The fallback keeps the requested water resolution"), static_cast<int32>(GpuPreferenceRun.Resolution), static_cast<int32>(FallbackGridResolution));
	TestEqual(TEXT("The fallback state checksum is bit-identical to the Cpu setting"), static_cast<uint32>(GpuPreferenceRun.StateChecksum), static_cast<uint32>(CpuPreferenceRun.StateChecksum));
	TestEqual(TEXT("The fallback water checksum is bit-identical to the Cpu setting"), static_cast<uint32>(GpuPreferenceRun.WaterChecksum), static_cast<uint32>(CpuPreferenceRun.WaterChecksum));
	TestTrue(TEXT("The fallback density-pressure pixels are bit-identical to the Cpu setting"), ArePixelsBitIdentical(CpuPreferenceRun.DensityPressurePixels, GpuPreferenceRun.DensityPressurePixels));
	TestTrue(TEXT("The fallback velocity pixels are bit-identical to the Cpu setting"), ArePixelsBitIdentical(CpuPreferenceRun.VelocityPixels, GpuPreferenceRun.VelocityPixels));
	TestTrue(TEXT("The fallback water surface pixels are bit-identical to the Cpu setting"), ArePixelsBitIdentical(CpuPreferenceRun.WaterSurfacePixels, GpuPreferenceRun.WaterSurfacePixels));
	return !HasAnyErrors();
}

#endif
