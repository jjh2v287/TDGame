#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidSimStateCacheTypes.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFluidSimStateCacheRhiTestPrivate
{
	constexpr float SimStateCacheRhiStepSeconds = 1.0f / 60.0f;
	constexpr int32 SimStateCacheRhiResolution = 128;
	constexpr int32 SimStateCacheRhiStampedSteps = 12;
	constexpr int32 SimStateCacheRhiSettleSteps = 6;
	constexpr int32 SimStateCacheRhiCostResolution = 512;
	constexpr int32 SimStateCacheRhiCostSampleSteps = 10;

	bool CanRunSimStateCacheRhiTest(FAutomationTestBase& Test, const TCHAR* RhiTestLabel)
	{
		if (!GUsingNullRHI && FApp::CanEverRender())
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), RhiTestLabel));
		return false;
	}

	struct FTDSimStateCacheConsoleScope
	{
		FTDSimStateCacheConsoleScope(const TCHAR* VariableName, int32 NewValue)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(VariableName);
			if (!Variable)
			{
				return;
			}

			PreviousValue = Variable->GetString();
			Variable->Set(NewValue, ECVF_SetByCode);
		}

		~FTDSimStateCacheConsoleScope()
		{
			if (Variable)
			{
				Variable->Set(*PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		FString PreviousValue;
	};

	struct FTDSimStateCacheRhiWorld
	{
		FTDSimStateCacheRhiWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidSimStateCacheRhiWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDSimStateCacheRhiWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			FlushRenderingCommands();
		}

		double StepWorldAndWaitForGpu()
		{
			const double StartSeconds = FPlatformTime::Seconds();
			++GFrameCounter;
			World->Tick(LEVELTICK_All, SimStateCacheRhiStepSeconds);
			ENQUEUE_RENDER_COMMAND(TDFluidSimStateCacheRhiWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
			return (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		}

		UWorld* World = nullptr;
	};

	FTDFluidSimulationConfig MakeGpuSmokeConfig(int32 Resolution)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Smoke);
		Config.GridResolution = Resolution;
		Config.SolverBackend = ETDFluidSolverBackend::Gpu;
		Config.AreaAddressMode = ETDFluidAreaAddressMode::Clamp;
		Config.bEnableWaterLayer = false;
		Config.AmbientDensity = 0.0f;
		Config.AmbientRefillPerSec = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		return Config;
	}

	FTDFluidSimulationConfig MakeGpuWaterConfig(int32 Resolution)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Water);
		Config.GridResolution = Resolution;
		Config.SolverBackend = ETDFluidSolverBackend::Gpu;
		Config.AreaAddressMode = ETDFluidAreaAddressMode::Clamp;
		Config.bEnableWaterLayer = true;
		Config.bEnableWaterWetness = true;
		Config.RippleWaveSpeedCmPerSec = FMath::Max(Config.RippleWaveSpeedCmPerSec, 200.0f);
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		return Config;
	}

	ATDFluidLiveActor* SpawnGpuActor(UWorld& World, const FTDFluidSimulationConfig& Config)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, FVector::ZeroVector);
		ATDFluidLiveActor* LiveActor = World.SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!LiveActor || !LiveActor->LiveComponent)
		{
			return nullptr;
		}

		LiveActor->LiveComponent->SimulationConfig = Config;
		LiveActor->FinishSpawning(SpawnTransform);
		return LiveActor;
	}

	void QueueCacheStamp(UTDFluidLiveComponent& Component, int32 StepIndex, float RippleImpulseCm)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(-60.0 + 8.0 * StepIndex, 15.0, 0.0);
		Stamp.WorldVelocity = FVector(260.0, 40.0, 0.0);
		Stamp.RadiusCm = 60.0f;
		Stamp.DensityStrength = 0.9f;
		Stamp.VelocityStrength = 1.0f;
		Stamp.RippleImpulseCm = RippleImpulseCm;
		Component.QueueBrushStamp(Stamp);
	}

	void RunStampedSteps(FTDSimStateCacheRhiWorld& TestWorld, UTDFluidLiveComponent& Component, int32 StampedSteps, int32 SettleSteps, float RippleImpulseCm)
	{
		for (int32 StepIndex = 0; StepIndex < StampedSteps; ++StepIndex)
		{
			QueueCacheStamp(Component, StepIndex, RippleImpulseCm);
			TestWorld.StepWorldAndWaitForGpu();
		}

		for (int32 StepIndex = 0; StepIndex < SettleSteps; ++StepIndex)
		{
			TestWorld.StepWorldAndWaitForGpu();
		}
	}

	bool CaptureGpuSnapshot(FAutomationTestBase& Test, UTDFluidLiveComponent& Component, const FString& Label, FTDFluidSimStateSnapshot& OutSnapshot)
	{
		FString Error;
		if (!Component.CaptureSimStateSnapshot(OutSnapshot, Error))
		{
			Test.AddError(FString::Printf(TEXT("%s: GPU capture failed: %s"), *Label, *Error));
			return false;
		}

		if (OutSnapshot.Backend != ETDFluidSimStateCacheBackend::Gpu || OutSnapshot.Channels.IsEmpty())
		{
			Test.AddError(FString::Printf(TEXT("%s: GPU capture returned backend %d with %d channels"), *Label, static_cast<int32>(OutSnapshot.Backend), OutSnapshot.Channels.Num()));
			return false;
		}

		return true;
	}

	bool HasNonZeroByte(const FTDFluidStateChannelBytes& Channel)
	{
		return Channel.Bytes.ContainsByPredicate([](uint8 Value) { return Value != 0; });
	}

	FString DescribeChannelDifference(const FTDFluidSimStateSnapshot& Expected, const FTDFluidSimStateSnapshot& Actual)
	{
		if (Expected.Channels.Num() != Actual.Channels.Num())
		{
			return FString::Printf(TEXT("channel count %d does not match %d"), Actual.Channels.Num(), Expected.Channels.Num());
		}

		if (!(Expected.GpuLayout == Actual.GpuLayout))
		{
			return TEXT("GPU layout differs");
		}

		for (const FTDFluidStateChannelBytes& ExpectedChannel : Expected.Channels)
		{
			const FTDFluidStateChannelBytes* ActualChannel = Actual.FindChannel(ExpectedChannel.Name);
			if (!ActualChannel)
			{
				return FString::Printf(TEXT("channel %s is missing"), *ExpectedChannel.Name.ToString());
			}

			if (ActualChannel->Width != ExpectedChannel.Width || ActualChannel->Height != ExpectedChannel.Height || ActualChannel->PixelFormat != ExpectedChannel.PixelFormat || ActualChannel->Bytes.Num() != ExpectedChannel.Bytes.Num())
			{
				return FString::Printf(TEXT("channel %s shape or format differs"), *ExpectedChannel.Name.ToString());
			}

			int32 DifferentByteCount = 0;
			int32 FirstDifferentByte = INDEX_NONE;
			for (int32 ByteIndex = 0; ByteIndex < ExpectedChannel.Bytes.Num(); ++ByteIndex)
			{
				if (ExpectedChannel.Bytes[ByteIndex] == ActualChannel->Bytes[ByteIndex])
				{
					continue;
				}

				FirstDifferentByte = FirstDifferentByte == INDEX_NONE ? ByteIndex : FirstDifferentByte;
				++DifferentByteCount;
			}

			if (DifferentByteCount > 0)
			{
				return FString::Printf(TEXT("channel %s differs in %d of %d bytes, first at byte %d"), *ExpectedChannel.Name.ToString(), DifferentByteCount, ExpectedChannel.Bytes.Num(), FirstDifferentByte);
			}
		}

		return FString();
	}

	void QueueSnapshotSeed(UTDFluidLiveComponent& Component, const FTDFluidSimStateSnapshot& Snapshot)
	{
		Component.QueueSimStateSeed(MakeShared<const FTDFluidSimStateSnapshot, ESPMode::ThreadSafe>(Snapshot));
	}

	double ComputeMeanStepMilliseconds(FTDSimStateCacheRhiWorld& TestWorld, int32 StepCount)
	{
		double TotalMilliseconds = 0.0;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			TotalMilliseconds += TestWorld.StepWorldAndWaitForGpu();
		}

		return StepCount > 0 ? TotalMilliseconds / StepCount : 0.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSimStateCacheGpuSeedStepMatchesOriginalTest,
	"TDGame.FluidFXRhi.SimStateCache.GpuSeedStepMatchesOriginal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidSimStateCacheGpuSeedStepMatchesOriginalTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSimStateCacheRhiTestPrivate;

	if (!CanRunSimStateCacheRhiTest(*this, TEXT("GpuSeedStepMatchesOriginal")))
	{
		return false;
	}

	FTDSimStateCacheRhiWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnGpuActor(*TestWorld.World, MakeGpuSmokeConfig(SimStateCacheRhiResolution));
	if (!TestNotNull(TEXT("GPU smoke actor spawns"), LiveActor))
	{
		return false;
	}

	UTDFluidLiveComponent& Component = *LiveActor->LiveComponent;
	RunStampedSteps(TestWorld, Component, SimStateCacheRhiStampedSteps, SimStateCacheRhiSettleSteps, 0.0f);
	if (!TestTrue(TEXT("Smoke actor runs on the GPU solver"), Component.IsUsingGpuSolver()))
	{
		return false;
	}

	FTDFluidSimStateSnapshot OriginalState;
	FTDFluidSimStateSnapshot OriginalNextState;
	if (!CaptureGpuSnapshot(*this, Component, TEXT("original S_N"), OriginalState))
	{
		return false;
	}

	TestWorld.StepWorldAndWaitForGpu();
	if (!CaptureGpuSnapshot(*this, Component, TEXT("original S_N+1"), OriginalNextState))
	{
		return false;
	}

	const FTDFluidStateChannelBytes* DensityChannel = OriginalState.FindChannel(TEXT("Density"));
	TestTrue(TEXT("Captured GPU state has velocity, density and pressure channels"), DensityChannel && OriginalState.FindChannel(TEXT("Velocity")) && OriginalState.FindChannel(TEXT("PressureSolution")));
	TestTrue(TEXT("Captured GPU density is not empty"), DensityChannel && HasNonZeroByte(*DensityChannel));
	TestTrue(TEXT("One more step changes the GPU state"), !DescribeChannelDifference(OriginalState, OriginalNextState).IsEmpty());

	Component.ResetSimulationState();
	QueueSnapshotSeed(Component, OriginalState);
	TestWorld.StepWorldAndWaitForGpu();
	TestFalse(TEXT("Seed is consumed once"), Component.HasPendingSimStateSeed());

	FTDFluidSimStateSnapshot SeededState;
	if (!CaptureGpuSnapshot(*this, Component, TEXT("seeded S_N"), SeededState))
	{
		return false;
	}

	const FString SeededDifference = DescribeChannelDifference(OriginalState, SeededState);
	TestTrue(FString::Printf(TEXT("Seeded GPU state equals S_N byte for byte (%s)"), *SeededDifference), SeededDifference.IsEmpty());
	TestEqual(TEXT("Seed restores the simulation time"), SeededState.SimulationTimeSeconds, OriginalState.SimulationTimeSeconds);

	TestWorld.StepWorldAndWaitForGpu();
	FTDFluidSimStateSnapshot SeededNextState;
	if (!CaptureGpuSnapshot(*this, Component, TEXT("seeded S_N+1"), SeededNextState))
	{
		return false;
	}

	const FString NextDifference = DescribeChannelDifference(OriginalNextState, SeededNextState);
	TestTrue(FString::Printf(TEXT("Step after the seed equals S_N+1 byte for byte (%s)"), *NextDifference), NextDifference.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSimStateCacheGpuWaterChannelsRoundTripTest,
	"TDGame.FluidFXRhi.SimStateCache.GpuWaterChannelsRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidSimStateCacheGpuWaterChannelsRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSimStateCacheRhiTestPrivate;

	if (!CanRunSimStateCacheRhiTest(*this, TEXT("GpuWaterChannelsRoundTrip")))
	{
		return false;
	}

	FTDSimStateCacheConsoleScope WaterGpuScope(TEXT("TD.FluidFX.WaterGpu"), 1);
	FTDSimStateCacheRhiWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnGpuActor(*TestWorld.World, MakeGpuWaterConfig(SimStateCacheRhiResolution));
	if (!TestNotNull(TEXT("GPU water actor spawns"), LiveActor))
	{
		return false;
	}

	UTDFluidLiveComponent& Component = *LiveActor->LiveComponent;
	RunStampedSteps(TestWorld, Component, SimStateCacheRhiStampedSteps, 2, 20.0f);
	if (!TestTrue(TEXT("Water actor runs on the GPU solver"), Component.IsUsingGpuSolver()))
	{
		return false;
	}

	FTDFluidSimStateSnapshot WaterState;
	if (!CaptureGpuSnapshot(*this, Component, TEXT("water state"), WaterState))
	{
		return false;
	}

	const FTDFluidStateChannelBytes* RippleChannel = WaterState.FindChannel(TEXT("Ripple"));
	TestTrue(TEXT("Water layout is captured"), WaterState.GpuLayout.bUsesWaterLayer && WaterState.bUsesWaterLayer);
	TestNotNull(TEXT("Water capture has the Ripple channel"), RippleChannel);
	TestNotNull(TEXT("Water capture has the WaterWetness channel"), WaterState.FindChannel(TEXT("WaterWetness")));
	if (!RippleChannel)
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("Ripple channel has non-zero bytes: %s"), HasNonZeroByte(*RippleChannel) ? TEXT("yes") : TEXT("no")));

	Component.ResetSimulationState();
	QueueSnapshotSeed(Component, WaterState);
	TestWorld.StepWorldAndWaitForGpu();
	FTDFluidSimStateSnapshot SeededState;
	if (!CaptureGpuSnapshot(*this, Component, TEXT("seeded water state"), SeededState))
	{
		return false;
	}

	const FString SeededDifference = DescribeChannelDifference(WaterState, SeededState);
	TestTrue(FString::Printf(TEXT("Seeded water state reads back unchanged (%s)"), *SeededDifference), SeededDifference.IsEmpty());

	FTDFluidSimStateSnapshot MismatchedState = WaterState;
	FTDFluidStateChannelBytes* MismatchedRipple = MismatchedState.Channels.FindByPredicate([](const FTDFluidStateChannelBytes& Channel) { return Channel.Name == FName(TEXT("Ripple")); });
	MismatchedRipple->PixelFormat = MismatchedRipple->PixelFormat == static_cast<uint8>(PF_G16R16F) ? static_cast<uint8>(PF_G32R32F) : static_cast<uint8>(PF_G16R16F);
	AddExpectedMessage(TEXT("GPU simulation state seed rejected"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	Component.ResetSimulationState();
	QueueSnapshotSeed(Component, MismatchedState);
	TestWorld.StepWorldAndWaitForGpu();
	FTDFluidSimStateSnapshot RejectedState;
	if (!CaptureGpuSnapshot(*this, Component, TEXT("rejected water state"), RejectedState))
	{
		return false;
	}

	TestFalse(TEXT("A seed with a different ripple format is not applied"), DescribeChannelDifference(WaterState, RejectedState).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFx16bOffscreenCostTest,
	"TDGame.FluidFXRhi.Fx16b.OffscreenCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidFx16bOffscreenCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSimStateCacheRhiTestPrivate;

	if (!CanRunSimStateCacheRhiTest(*this, TEXT("Fx16b.OffscreenCost")))
	{
		return false;
	}

	FTDSimStateCacheRhiWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnGpuActor(*TestWorld.World, MakeGpuSmokeConfig(SimStateCacheRhiCostResolution));
	if (!TestNotNull(TEXT("512 GPU smoke actor spawns"), LiveActor))
	{
		return false;
	}

	UTDFluidLiveComponent& Component = *LiveActor->LiveComponent;
	RunStampedSteps(TestWorld, Component, SimStateCacheRhiStampedSteps, SimStateCacheRhiSettleSteps, 0.0f);
	if (!TestTrue(TEXT("Cost actor runs on the GPU solver"), Component.IsUsingGpuSolver()))
	{
		return false;
	}

	const double BaselineStepMilliseconds = ComputeMeanStepMilliseconds(TestWorld, SimStateCacheRhiCostSampleSteps);
	FTDFluidSimStateSnapshot CostState;
	const double CaptureStartSeconds = FPlatformTime::Seconds();
	if (!CaptureGpuSnapshot(*this, Component, TEXT("cost capture"), CostState))
	{
		return false;
	}
	const double CaptureMilliseconds = (FPlatformTime::Seconds() - CaptureStartSeconds) * 1000.0;

	Component.ResetSimulationState();
	QueueSnapshotSeed(Component, CostState);
	const double SeedStepMilliseconds = TestWorld.StepWorldAndWaitForGpu();

	TArray<FString> QualityEntries;
	{
		FTDSimStateCacheConsoleScope QualityScope(TEXT("TD.FluidFX.OutputQuality"), 3);
		for (int32 QualityLevel = 0; QualityLevel <= 3; ++QualityLevel)
		{
			if (QualityScope.Variable)
			{
				QualityScope.Variable->Set(QualityLevel, ECVF_SetByCode);
			}
			IConsoleManager::Get().CallAllConsoleVariableSinks();
			QualityEntries.Add(FString::Printf(TEXT("{\"level\": %d, \"step_ms\": %.4f}"), QualityLevel, ComputeMeanStepMilliseconds(TestWorld, SimStateCacheRhiCostSampleSteps)));
		}
	}
	IConsoleManager::Get().CallAllConsoleVariableSinks();

	double DebugStepMilliseconds = 0.0;
	{
		FTDSimStateCacheConsoleScope DebugScope(TEXT("TD.FluidFX.Debug"), 15);
		DebugStepMilliseconds = ComputeMeanStepMilliseconds(TestWorld, SimStateCacheRhiCostSampleSteps);
	}

	const FString OutputDirectory = FPaths::ProjectSavedDir() / TEXT("AgentOps") / FDateTime::Now().ToString(TEXT("%Y%m%d")) / TEXT("fx16b");
	const FString OutputPath = OutputDirectory / TEXT("cost.json");
	const FString CostJson = FString::Printf(
		TEXT("{\n  \"resolution\": %d,\n  \"capture_ms\": %.4f,\n  \"baseline_step_ms\": %.4f,\n  \"seed_step_ms\": %.4f,\n  \"seed_extra_ms\": %.4f,\n  \"output_quality_step_ms\": [%s],\n  \"debug_15_step_ms_per_actor\": %.4f,\n  \"debug_15_extra_ms_per_actor\": %.4f,\n  \"channel_count\": %d,\n  \"timing\": \"wall clock per world tick including SubmitAndBlockUntilGPUIdle\",\n  \"svol_box\": \"not measured here: this headless world has no scene view\"\n}\n"),
		SimStateCacheRhiCostResolution,
		CaptureMilliseconds,
		BaselineStepMilliseconds,
		SeedStepMilliseconds,
		SeedStepMilliseconds - BaselineStepMilliseconds,
		*FString::Join(QualityEntries, TEXT(", ")),
		DebugStepMilliseconds,
		DebugStepMilliseconds - BaselineStepMilliseconds,
		CostState.Channels.Num());
	TestTrue(TEXT("Cost report is written"), FFileHelper::SaveStringToFile(CostJson, *OutputPath));
	AddInfo(FString::Printf(TEXT("Fx16b offscreen cost written to %s"), *OutputPath));
	return true;
}

#endif
