#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidRuntimeOps.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFluidOpsActorTestPrivate
{
	const TCHAR* const OpsActorMaxStepRateVariable = TEXT("TD.FluidFX.MaxStepRateHz");
	const TCHAR* const OpsActorPauseVariable = TEXT("TD.FluidFX.PauseWhenNotVisible");
	constexpr int32 OpsActorBaseFrameRate = 60;
	constexpr int32 OpsActorFastFrameRate = 120;
	constexpr double OpsActorDummyStartXCm = -200.0;
	constexpr double OpsActorDummySpeedCmPerSec = 300.0;
	constexpr double OpsActorSlowDummyStepCm = 2.0;
	constexpr int32 OpsActorPausedTickCount = 10;
	constexpr int32 OpsActorCostActorCount = 16;
	constexpr int32 OpsActorCostTickCount = 240;
	constexpr double OpsActorCostSpacingCm = 4000.0;
	constexpr double OpsActorPausedCostUpperBoundMs = 1.0;
	constexpr int32 OpsActorCostCpuActorCount = 8;
	constexpr int32 OpsActorCostGpuActorCount = 2;
	constexpr int32 OpsActorCostCpuResolution = 128;
	constexpr int32 OpsActorCostGpuResolution = 256;
	constexpr double OpsActorUnrenderedSeconds = 10.0;

	struct FTDFluidOpsActorTestWorld
	{
		FTDFluidOpsActorTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidOpsActorTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidOpsActorTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		UTDFluidLiveSubsystem* GetFluidSubsystem() const
		{
			return World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
		}

		ATDFluidLiveActor* SpawnMudActor(const FVector& Location) const
		{
			UTDFluidLiveSubsystem* FluidSubsystem = GetFluidSubsystem();
			UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(World, ETDFluidPresetKind::Mud);
			return FluidSubsystem ? FluidSubsystem->SpawnPooledFluidActor(Preset, Location, 0.0f) : nullptr;
		}

		ATDFluidLiveActor* SpawnConfiguredActor(const FVector& Location, const FTDFluidSimulationConfig& Config) const
		{
			const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
			ATDFluidLiveActor* LiveActor = World->SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
			if (!LiveActor || !LiveActor->LiveComponent)
			{
				return nullptr;
			}

			LiveActor->LiveComponent->SimulationConfig = Config;
			LiveActor->FinishSpawning(SpawnTransform);
			return LiveActor;
		}

		void AdvanceClockPastRenderGrace() const
		{
			World->TimeSeconds += OpsActorUnrenderedSeconds;
		}

		void TickFluid(float DeltaSeconds) const
		{
			if (UTDFluidLiveSubsystem* FluidSubsystem = GetFluidSubsystem())
			{
				FluidSubsystem->Tick(DeltaSeconds);
			}
		}

		UWorld* World = nullptr;
	};

	void SetOpsConsoleValue(const TCHAR* VariableName, const TCHAR* Value)
	{
		if (IConsoleVariable* ConsoleVariable = IConsoleManager::Get().FindConsoleVariable(VariableName))
		{
			ConsoleVariable->Set(Value, ECVF_SetByCode);
		}
	}

	void RestoreOpsConsoleValues()
	{
		SetOpsConsoleValue(OpsActorMaxStepRateVariable, TEXT("-1"));
		SetOpsConsoleValue(OpsActorPauseVariable, TEXT("-1"));
	}

	AActor* SpawnMovingDummy(UWorld& World, const FVector& Location, bool bHasOverlapBox)
	{
		AActor* Dummy = World.SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!Dummy)
		{
			return nullptr;
		}

		if (bHasOverlapBox)
		{
			UBoxComponent* OverlapBox = NewObject<UBoxComponent>(Dummy);
			OverlapBox->SetBoxExtent(FVector(30.0));
			OverlapBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			OverlapBox->SetCollisionObjectType(ECC_Pawn);
			OverlapBox->SetCollisionResponseToAllChannels(ECR_Overlap);
			Dummy->AddInstanceComponent(OverlapBox);
			Dummy->SetRootComponent(OverlapBox);
			OverlapBox->RegisterComponent();
		}
		else
		{
			USceneComponent* DummyRoot = NewObject<USceneComponent>(Dummy);
			Dummy->AddInstanceComponent(DummyRoot);
			Dummy->SetRootComponent(DummyRoot);
			DummyRoot->RegisterComponent();
		}

		Dummy->SetActorLocation(Location);
		return Dummy;
	}

	FVector ComputeDummyLocation(int32 FrameIndex, int32 FrameRate)
	{
		const double ElapsedSeconds = static_cast<double>(FrameIndex) / static_cast<double>(FrameRate);
		return FVector(OpsActorDummyStartXCm + OpsActorDummySpeedCmPerSec * ElapsedSeconds, 0.0, 0.0);
	}

	void QueueCenterStamp(UTDFluidLiveComponent& Component, const FVector& Location)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = Location;
		Stamp.WorldVelocity = FVector(200.0, 0.0, 0.0);
		Stamp.RadiusCm = 150.0f;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 1.0f;
		Component.QueueBrushStamp(Stamp);
	}

	struct FTDFluidOpsCadenceRun
	{
		uint32 StateChecksum = 0;
		int32 CompletedStepCount = 0;
		int32 MaxProcessedPointCount = 0;
		bool bIsValid = false;
	};

	FTDFluidOpsCadenceRun RunMovingDummyCadence(int32 FrameRate, int32 FrameCount, bool bUsesOverlapBox)
	{
		FTDFluidOpsCadenceRun Run;
		FTDFluidOpsActorTestWorld TestWorld;
		ATDFluidLiveActor* LiveActor = TestWorld.SpawnMudActor(FVector::ZeroVector);
		AActor* Dummy = SpawnMovingDummy(*TestWorld.World, ComputeDummyLocation(0, FrameRate), bUsesOverlapBox);
		if (!LiveActor || !LiveActor->LiveComponent || !Dummy)
		{
			return Run;
		}

		if (!bUsesOverlapBox)
		{
			LiveActor->RegisterInteractingActor(Dummy);
		}

		const float FrameDeltaSeconds = 1.0f / static_cast<float>(FrameRate);
		for (int32 FrameIndex = 1; FrameIndex <= FrameCount; ++FrameIndex)
		{
			Dummy->SetActorLocation(ComputeDummyLocation(FrameIndex, FrameRate));
			TestWorld.TickFluid(FrameDeltaSeconds);
			Run.MaxProcessedPointCount = FMath::Max(Run.MaxProcessedPointCount, LiveActor->LiveComponent->GetLastProcessedPointCount());
		}

		Run.StateChecksum = LiveActor->LiveComponent->GetSolver().ComputeStateChecksum();
		Run.CompletedStepCount = LiveActor->GetCompletedStepCount();
		Run.bIsValid = LiveActor->LiveComponent->GetSolver().IsInitialized();
		return Run;
	}

	FString MakeOpsReportPath(const TCHAR* FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentOps"), FDateTime::Now().ToString(TEXT("%Y%m%d")), TEXT("fx16a"), FileName));
	}

	struct FTDFluidOpsCostPass
	{
		double MeanTickMs = 0.0;
		int32 CpuStepCount = 0;
		int32 GpuStepCount = 0;
		bool bUsedGpu = true;
	};

	FTDFluidOpsCostPass RunOffscreenCostPass(const TCHAR* MaxStepRateValue)
	{
		SetOpsConsoleValue(OpsActorMaxStepRateVariable, MaxStepRateValue);
		FTDFluidOpsCostPass Pass;
		FTDFluidOpsActorTestWorld TestWorld;

		FTDFluidSimulationConfig CpuConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Mud);
		CpuConfig.GridResolution = OpsActorCostCpuResolution;
		CpuConfig.SolverBackend = ETDFluidSolverBackend::Cpu;
		FTDFluidSimulationConfig GpuConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Smoke);
		GpuConfig.GridResolution = OpsActorCostGpuResolution;
		GpuConfig.SolverBackend = ETDFluidSolverBackend::Gpu;
		GpuConfig.bEnableWaterLayer = false;

		TArray<ATDFluidLiveActor*> CpuActors;
		TArray<ATDFluidLiveActor*> GpuActors;
		for (int32 ActorIndex = 0; ActorIndex < OpsActorCostCpuActorCount + OpsActorCostGpuActorCount; ++ActorIndex)
		{
			const bool bIsGpuActor = ActorIndex >= OpsActorCostCpuActorCount;
			const FVector Location(OpsActorCostSpacingCm * ActorIndex, 0.0, 0.0);
			if (ATDFluidLiveActor* LiveActor = TestWorld.SpawnConfiguredActor(Location, bIsGpuActor ? GpuConfig : CpuConfig))
			{
				(bIsGpuActor ? GpuActors : CpuActors).Add(LiveActor);
			}
		}

		const float FrameDeltaSeconds = 1.0f / static_cast<float>(OpsActorFastFrameRate);
		double TickSeconds = 0.0;
		for (int32 FrameIndex = 0; FrameIndex < OpsActorCostTickCount; ++FrameIndex)
		{
			const uint64 StartCycles = FPlatformTime::Cycles64();
			TestWorld.TickFluid(FrameDeltaSeconds);
			TickSeconds += FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartCycles);
			FlushRenderingCommands();
		}

		Pass.MeanTickMs = TickSeconds * 1000.0 / OpsActorCostTickCount;
		for (const ATDFluidLiveActor* CpuActor : CpuActors)
		{
			Pass.CpuStepCount += CpuActor->GetCompletedStepCount();
		}
		for (const ATDFluidLiveActor* GpuActor : GpuActors)
		{
			Pass.GpuStepCount += GpuActor->GetCompletedStepCount();
			Pass.bUsedGpu &= GpuActor->LiveComponent && GpuActor->LiveComponent->IsUsingGpuSolver();
		}
		Pass.bUsedGpu &= GpuActors.Num() == OpsActorCostGpuActorCount;
		return Pass;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsActorStepRateCapTest,
	"TDGame.FluidFX.Ops.StepRateCapMatchesLowerFrameRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsActorStepRateCapTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOpsActorTestPrivate;
	ON_SCOPE_EXIT
	{
		RestoreOpsConsoleValues();
	};

	SetOpsConsoleValue(OpsActorMaxStepRateVariable, TEXT("-1"));
	const FTDFluidOpsCadenceRun BaseRun = RunMovingDummyCadence(OpsActorBaseFrameRate, OpsActorBaseFrameRate, false);
	SetOpsConsoleValue(OpsActorMaxStepRateVariable, TEXT("60"));
	const FTDFluidOpsCadenceRun CappedRun = RunMovingDummyCadence(OpsActorFastFrameRate, OpsActorFastFrameRate, false);

	if (!TestTrue(TEXT("Both cadence runs initialized a solver"), BaseRun.bIsValid && CappedRun.bIsValid))
	{
		return false;
	}

	TestEqual(TEXT("The uncapped 60 fps run steps every frame"), BaseRun.CompletedStepCount, OpsActorBaseFrameRate);
	TestEqual(TEXT("The 120 fps run capped at 60 Hz steps as often as the 60 fps run"), CappedRun.CompletedStepCount, BaseRun.CompletedStepCount);
	TestTrue(TEXT("The moving dummy stamped the fluid"), BaseRun.MaxProcessedPointCount > 0);
	TestEqual(TEXT("A 60 Hz cap at 120 fps leaves the same solver state as 60 fps"), CappedRun.StateChecksum, BaseRun.StateChecksum);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsActorMassInputRateTest,
	"TDGame.FluidFX.Ops.StepRateSkipsMassInputActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsActorMassInputRateTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOpsActorTestPrivate;
	ON_SCOPE_EXIT
	{
		RestoreOpsConsoleValues();
	};

	SetOpsConsoleValue(OpsActorMaxStepRateVariable, TEXT("60"));
	FTDFluidOpsActorTestWorld TestWorld;
	ATDFluidLiveActor* MassInputActor = TestWorld.SpawnMudActor(FVector::ZeroVector);
	ATDFluidLiveActor* CappedActor = TestWorld.SpawnMudActor(FVector(OpsActorCostSpacingCm, 0.0, 0.0));
	if (!TestNotNull(TEXT("Spawned the mass input actor"), MassInputActor) || !TestNotNull(TEXT("Spawned the capped actor"), CappedActor))
	{
		return false;
	}

	FTDFluidMassInputSettings MassInputSettings = MassInputActor->LiveComponent->GetMassInputSettings();
	MassInputSettings.bAcceptMassInput = true;
	MassInputActor->LiveComponent->SetMassInputSettingsForTests(MassInputSettings);
	if (!TestTrue(TEXT("The actor accepts mass input"), MassInputActor->LiveComponent->AcceptsMassInput()))
	{
		return false;
	}

	TestEqual(TEXT("Mass input actors resolve no step rate cap"), MassInputActor->ResolveStepRateHz(), 0.0f);
	TestEqual(TEXT("Other actors resolve the forced cap"), CappedActor->ResolveStepRateHz(), 60.0f);

	const float FrameDeltaSeconds = 1.0f / static_cast<float>(OpsActorFastFrameRate);
	for (int32 FrameIndex = 0; FrameIndex < OpsActorFastFrameRate; ++FrameIndex)
	{
		TestWorld.TickFluid(FrameDeltaSeconds);
	}

	TestEqual(TEXT("The mass input actor steps every frame"), MassInputActor->GetCompletedStepCount(), OpsActorFastFrameRate);
	TestEqual(TEXT("The capped actor steps every other frame"), CappedActor->GetCompletedStepCount(), OpsActorFastFrameRate / 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsActorPauseHoldsStateTest,
	"TDGame.FluidFX.Ops.VisibilityPauseHoldsState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsActorPauseHoldsStateTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOpsActorTestPrivate;
	ON_SCOPE_EXIT
	{
		RestoreOpsConsoleValues();
	};

	SetOpsConsoleValue(OpsActorPauseVariable, TEXT("2"));
	FTDFluidOpsActorTestWorld TestWorld;
	TestWorld.AdvanceClockPastRenderGrace();
	ATDFluidLiveActor* LiveActor = TestWorld.SpawnMudActor(FVector::ZeroVector);
	const FVector DummyStart(OpsActorDummyStartXCm, 0.0, 0.0);
	AActor* Dummy = SpawnMovingDummy(*TestWorld.World, DummyStart, false);
	if (!TestNotNull(TEXT("Spawned a mud actor"), LiveActor) || !TestNotNull(TEXT("Spawned a dummy"), Dummy))
	{
		return false;
	}

	UTDFluidLiveComponent& Component = *LiveActor->LiveComponent;
	if (!TestTrue(TEXT("Mud is eligible for the visibility pause"), Component.IsEligibleForVisibilityPause()))
	{
		return false;
	}

	LiveActor->RegisterInteractingActor(Dummy);
	const float FrameDeltaSeconds = 1.0f / static_cast<float>(OpsActorBaseFrameRate);
	QueueCenterStamp(Component, FVector::ZeroVector);
	TestWorld.TickFluid(FrameDeltaSeconds);
	TestEqual(TEXT("The first awake tick steps before any pause"), LiveActor->GetCompletedStepCount(), 1);

	const uint32 ChecksumBeforePause = Component.GetSolver().ComputeStateChecksum();
	int32 MaxPendingStampCount = 0;
	for (int32 TickIndex = 1; TickIndex <= OpsActorPausedTickCount; ++TickIndex)
	{
		Dummy->SetActorLocation(DummyStart + FVector(OpsActorSlowDummyStepCm * TickIndex, 0.0, 0.0));
		QueueCenterStamp(Component, FVector::ZeroVector);
		TestWorld.TickFluid(FrameDeltaSeconds);
		MaxPendingStampCount = FMath::Max(MaxPendingStampCount, Component.GetPendingBrushStamps().Num());
	}

	TestEqual(TEXT("A paused actor keeps its solver state"), Component.GetSolver().ComputeStateChecksum(), ChecksumBeforePause);
	TestEqual(TEXT("A paused actor completes no steps"), LiveActor->GetCompletedStepCount(), 1);
	TestTrue(TEXT("A paused actor stays awake"), Component.IsSimulationAwake());
	TestEqual(TEXT("A paused actor discards pending stamps"), MaxPendingStampCount, 0);

	SetOpsConsoleValue(OpsActorPauseVariable, TEXT("-1"));
	Dummy->SetActorLocation(DummyStart + FVector(OpsActorSlowDummyStepCm * (OpsActorPausedTickCount + 1), 0.0, 0.0));
	TestWorld.TickFluid(FrameDeltaSeconds);
	TestEqual(TEXT("The resumed actor steps again"), LiveActor->GetCompletedStepCount(), 2);
	TestEqual(TEXT("The first resumed step stamps nothing from the pre-pause position"), Component.GetLastProcessedPointCount(), 0);

	Dummy->SetActorLocation(DummyStart + FVector(OpsActorSlowDummyStepCm * (OpsActorPausedTickCount + 2), 0.0, 0.0));
	TestWorld.TickFluid(FrameDeltaSeconds);
	TestEqual(TEXT("The second resumed step runs"), LiveActor->GetCompletedStepCount(), 3);
	TestTrue(TEXT("The second resumed step stamps the moving dummy"), Component.GetLastProcessedPointCount() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsActorPauseViewportTest,
	"TDGame.FluidFX.Ops.VisibilityPauseNeedsViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsActorPauseViewportTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOpsActorTestPrivate;
	ON_SCOPE_EXIT
	{
		RestoreOpsConsoleValues();
	};

	SetOpsConsoleValue(OpsActorPauseVariable, TEXT("1"));
	FTDFluidOpsActorTestWorld TestWorld;
	TestWorld.AdvanceClockPastRenderGrace();
	ATDFluidLiveActor* LiveActor = TestWorld.SpawnMudActor(FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned a mud actor"), LiveActor))
	{
		return false;
	}

	TestFalse(TEXT("A world without a game viewport cannot judge visibility"), TDFluidRuntimeOps::CanJudgeVisibility(*TestWorld.World));
	const int32 TickCount = OpsActorPausedTickCount + 1;
	for (int32 TickIndex = 0; TickIndex < TickCount; ++TickIndex)
	{
		TestWorld.TickFluid(1.0f / static_cast<float>(OpsActorBaseFrameRate));
	}

	TestEqual(TEXT("Mode 1 never pauses without a viewport"), LiveActor->GetCompletedStepCount(), TickCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsActorTransformDrivenTest,
	"TDGame.FluidFX.Ops.TransformDrivenActorMatchesRegistered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsActorTransformDrivenTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOpsActorTestPrivate;
	ON_SCOPE_EXIT
	{
		RestoreOpsConsoleValues();
	};

	RestoreOpsConsoleValues();
	const FTDFluidOpsCadenceRun RegisteredRun = RunMovingDummyCadence(OpsActorBaseFrameRate, OpsActorBaseFrameRate, false);
	const FTDFluidOpsCadenceRun OverlapRun = RunMovingDummyCadence(OpsActorBaseFrameRate, OpsActorBaseFrameRate, true);
	if (!TestTrue(TEXT("Both runs initialized a solver"), RegisteredRun.bIsValid && OverlapRun.bIsValid))
	{
		return false;
	}

	TestTrue(TEXT("The transform-driven overlap box stamps the fluid"), OverlapRun.MaxProcessedPointCount > 0);
	TestEqual(TEXT("Moving by transform alone matches a registered actor"), OverlapRun.StateChecksum, RegisteredRun.StateChecksum);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsActorPausedCostTest,
	"TDGame.FluidFX.Ops.PausedActorCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsActorPausedCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOpsActorTestPrivate;
	ON_SCOPE_EXIT
	{
		RestoreOpsConsoleValues();
	};

	SetOpsConsoleValue(OpsActorPauseVariable, TEXT("2"));
	FTDFluidOpsActorTestWorld TestWorld;
	TestWorld.AdvanceClockPastRenderGrace();
	TArray<ATDFluidLiveActor*> LiveActors;
	for (int32 ActorIndex = 0; ActorIndex < OpsActorCostActorCount; ++ActorIndex)
	{
		if (ATDFluidLiveActor* LiveActor = TestWorld.SpawnMudActor(FVector(OpsActorCostSpacingCm * ActorIndex, 0.0, 0.0)))
		{
			LiveActors.Add(LiveActor);
		}
	}
	if (!TestEqual(TEXT("Spawned every mud actor"), LiveActors.Num(), OpsActorCostActorCount))
	{
		return false;
	}

	const float FrameDeltaSeconds = 1.0f / static_cast<float>(OpsActorFastFrameRate);
	TestWorld.TickFluid(FrameDeltaSeconds);
	const int32 MeasuredTickCount = OpsActorCostTickCount - 1;
	const uint64 StartCycles = FPlatformTime::Cycles64();
	for (int32 TickIndex = 0; TickIndex < MeasuredTickCount; ++TickIndex)
	{
		TestWorld.TickFluid(FrameDeltaSeconds);
	}
	const double MeasuredMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles);
	const double MeanMsPerPausedActorTick = MeasuredMs / (static_cast<double>(MeasuredTickCount) * OpsActorCostActorCount);

	int32 TotalStepCount = 0;
	for (const ATDFluidLiveActor* LiveActor : LiveActors)
	{
		TotalStepCount += LiveActor->GetCompletedStepCount();
	}
	TestEqual(TEXT("Every actor stepped once and then stayed paused"), TotalStepCount, OpsActorCostActorCount);

	const FString Report = FString::Printf(
		TEXT("{\n  \"paused_actor_count\": %d,\n  \"measured_tick_count\": %d,\n  \"tick_seconds\": %.9f,\n  \"total_ms\": %.6f,\n  \"mean_ms_per_paused_actor_tick\": %.6f,\n  \"target_ms\": 0.01,\n  \"upper_bound_ms\": %.3f\n}\n"),
		OpsActorCostActorCount, MeasuredTickCount, FrameDeltaSeconds, MeasuredMs, MeanMsPerPausedActorTick, OpsActorPausedCostUpperBoundMs);
	TestTrue(TEXT("Wrote the paused cost report"), FFileHelper::SaveStringToFile(Report, *MakeOpsReportPath(TEXT("paused_cost.json"))));
	TestTrue(*FString::Printf(TEXT("A paused actor costs at most %.3f ms per tick (measured %.6f ms)"), OpsActorPausedCostUpperBoundMs, MeanMsPerPausedActorTick), MeanMsPerPausedActorTick <= OpsActorPausedCostUpperBoundMs);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsActorOffscreenCostTest,
	"TDGame.FluidFXRhi.OpsCost.OffscreenStepRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsActorOffscreenCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOpsActorTestPrivate;
	ON_SCOPE_EXIT
	{
		RestoreOpsConsoleValues();
	};

	const FTDFluidOpsCostPass UncappedPass = RunOffscreenCostPass(TEXT("-1"));
	const FTDFluidOpsCostPass CappedPass = RunOffscreenCostPass(TEXT("60"));
	const double TickMsRatio = UncappedPass.MeanTickMs > 0.0 ? CappedPass.MeanTickMs / UncappedPass.MeanTickMs : 0.0;
	const bool bUsedGpu = UncappedPass.bUsedGpu && CappedPass.bUsedGpu;

	const FString Report = FString::Printf(
		TEXT("{\n  \"cpu_actor_count\": %d,\n  \"cpu_resolution\": %d,\n  \"gpu_actor_count\": %d,\n  \"gpu_resolution\": %d,\n  \"tick_count\": %d,\n  \"used_gpu\": %s,\n  \"uncapped_mean_tick_ms\": %.6f,\n  \"capped60_mean_tick_ms\": %.6f,\n  \"capped_to_uncapped_tick_ms_ratio\": %.6f,\n  \"uncapped_cpu_steps\": %d,\n  \"capped60_cpu_steps\": %d,\n  \"uncapped_gpu_steps\": %d,\n  \"capped60_gpu_steps\": %d\n}\n"),
		OpsActorCostCpuActorCount, OpsActorCostCpuResolution, OpsActorCostGpuActorCount, OpsActorCostGpuResolution, OpsActorCostTickCount,
		bUsedGpu ? TEXT("true") : TEXT("false"),
		UncappedPass.MeanTickMs, CappedPass.MeanTickMs, TickMsRatio,
		UncappedPass.CpuStepCount, CappedPass.CpuStepCount, UncappedPass.GpuStepCount, CappedPass.GpuStepCount);
	TestTrue(TEXT("Wrote the offscreen step rate cost report"), FFileHelper::SaveStringToFile(Report, *MakeOpsReportPath(TEXT("ops_cost.json"))));
	TestTrue(TEXT("The 60 Hz cap runs fewer CPU steps at 120 fps"), CappedPass.CpuStepCount < UncappedPass.CpuStepCount);
	if (!bUsedGpu)
	{
		AddInfo(TEXT("GPU solver unavailable; GPU step counts are recorded without comparison."));
		return true;
	}

	TestTrue(TEXT("The 60 Hz cap runs fewer GPU steps at 120 fps"), CappedPass.GpuStepCount < UncappedPass.GpuStepCount);
	return true;
}

#endif
