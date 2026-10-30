#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFluidOpsRhiTestPrivate
{
	constexpr float OpsRhiStepSeconds = 1.0f / 60.0f;
	constexpr int32 OpsRhiGpuResolution = 128;
	constexpr int32 OpsRhiGpuStepCount = 30;
	constexpr int32 OpsRhiCpuStepCount = 8;
	constexpr int32 OpsRhiMismatchResolution = 32;
	constexpr int32 OpsRhiMismatchStepCount = 5;
	const FLinearColor OpsRhiEmptyDensityPressureColor(0.0f, 0.5f, 0.0f, 0.0f);

	bool CanRunOpsRhiTest(FAutomationTestBase& Test, const TCHAR* RhiTestLabel)
	{
		if (!GUsingNullRHI && FApp::CanEverRender())
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), RhiTestLabel));
		return false;
	}

	struct FTDFluidOpsRhiTestWorld
	{
		FTDFluidOpsRhiTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidOpsRhiTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidOpsRhiTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			FlushRenderingCommands();
		}

		void StepWorldAndWaitForGpu()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, OpsRhiStepSeconds);
			ENQUEUE_RENDER_COMMAND(TDFluidOpsRhiTestWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	UTextureRenderTarget2D* CreateExternalTarget(int32 Resolution, TextureAddress Address)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_RGBA16f;
		Target->ClearColor = OpsRhiEmptyDensityPressureColor;
		Target->bAutoGenerateMips = false;
		Target->bSupportsUAV = true;
		Target->bCanCreateUAV = true;
		Target->Filter = TF_Bilinear;
		Target->AddressX = Address;
		Target->AddressY = Address;
		Target->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, true);
		Target->UpdateResourceImmediate(true);
		return Target;
	}

	ATDFluidLiveActor* SpawnGpuSmokeActor(UWorld& World)
	{
		FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Smoke);
		Config.GridResolution = OpsRhiGpuResolution;
		Config.SolverBackend = ETDFluidSolverBackend::Gpu;
		Config.AreaAddressMode = ETDFluidAreaAddressMode::Clamp;
		Config.bEnableWaterLayer = false;
		Config.AmbientDensity = 0.0f;
		Config.AmbientRefillPerSec = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;

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

	void QueueOpsStamp(UTDFluidLiveComponent& Component, int32 StepIndex)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector(-60.0 + 8.0 * StepIndex, 15.0, 0.0);
		Stamp.WorldVelocity = FVector(260.0, 40.0, 0.0);
		Stamp.RadiusCm = 60.0f;
		Stamp.DensityStrength = 0.9f;
		Stamp.VelocityStrength = 1.0f;
		Component.QueueBrushStamp(Stamp);
	}

	bool ReadRenderTargetPixels(UTexture* OutputTexture, TArray<FFloat16Color>& OutPixels)
	{
		OutPixels.Reset();
		UTextureRenderTarget2D* OutputTarget = Cast<UTextureRenderTarget2D>(OutputTexture);
		FTextureRenderTargetResource* TargetResource = OutputTarget ? OutputTarget->GameThread_GetRenderTargetResource() : nullptr;
		if (!TargetResource)
		{
			return false;
		}

		FRenderTarget* ReadableTarget = TargetResource;
		return ReadableTarget->ReadFloat16Pixels(OutPixels) && !OutPixels.IsEmpty();
	}

	bool ArePixelsBitIdentical(const TArray<FFloat16Color>& Left, const TArray<FFloat16Color>& Right)
	{
		return Left.Num() == Right.Num() && FMemory::Memcmp(Left.GetData(), Right.GetData(), Left.Num() * sizeof(FFloat16Color)) == 0;
	}

	struct FTDFluidOpsGpuRun
	{
		TArray<FFloat16Color> DensityPressurePixels;
		bool bUsedGpu = false;
		bool bWroteExternalTarget = false;
		bool bIsValid = false;
	};

	FTDFluidOpsGpuRun RunGpuScenario(UTextureRenderTarget2D* ExternalTarget)
	{
		FTDFluidOpsGpuRun Run;
		FTDFluidOpsRhiTestWorld TestWorld;
		ATDFluidLiveActor* LiveActor = SpawnGpuSmokeActor(*TestWorld.World);
		UTDFluidLiveComponent* Component = LiveActor ? LiveActor->LiveComponent.Get() : nullptr;
		if (!Component)
		{
			return Run;
		}

		Run.bUsedGpu = Component->IsUsingGpuSolver();
		if (!Run.bUsedGpu)
		{
			Run.bIsValid = true;
			return Run;
		}

		Component->ExternalOutputTargets.DensityPressureTarget = ExternalTarget;
		for (int32 StepIndex = 0; StepIndex < OpsRhiGpuStepCount; ++StepIndex)
		{
			QueueOpsStamp(*Component, StepIndex);
			TestWorld.StepWorldAndWaitForGpu();
		}

		UTexture* DensityPressureTexture = Component->GetDensityPressureTexture();
		Run.bWroteExternalTarget = ExternalTarget && DensityPressureTexture == ExternalTarget;
		Run.bIsValid = ReadRenderTargetPixels(DensityPressureTexture, Run.DensityPressurePixels);
		return Run;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsExternalTargetGpuDirectWriteTest,
	"TDGame.FluidFXRhi.Ops.ExternalTargetGpuDirectWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsExternalTargetGpuDirectWriteTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOpsRhiTestPrivate;
	if (!CanRunOpsRhiTest(*this, TEXT("Ops.ExternalTargetGpuDirectWrite")))
	{
		return false;
	}

	const FTDFluidOpsGpuRun InternalRun = RunGpuScenario(nullptr);
	if (!TestTrue(TEXT("The internal-target GPU run completed"), InternalRun.bIsValid))
	{
		return false;
	}
	if (!InternalRun.bUsedGpu)
	{
		AddInfo(TEXT("The GPU solver is not available here, so the direct-write comparison is skipped."));
		return true;
	}

	TStrongObjectPtr<UTextureRenderTarget2D> ExternalTarget(CreateExternalTarget(OpsRhiGpuResolution, TA_Clamp));
	const FTDFluidOpsGpuRun ExternalRun = RunGpuScenario(ExternalTarget.Get());
	if (!TestTrue(TEXT("The external-target GPU run completed"), ExternalRun.bIsValid && ExternalRun.bUsedGpu))
	{
		return false;
	}

	TestTrue(TEXT("The GPU solver writes the density-pressure output straight into the external target"), ExternalRun.bWroteExternalTarget);
	TestTrue(TEXT("The external target holds the same bits as the internal target"), ArePixelsBitIdentical(ExternalRun.DensityPressurePixels, InternalRun.DensityPressurePixels));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsExternalTargetCpuCopyTest,
	"TDGame.FluidFXRhi.Ops.ExternalTargetCpuCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsExternalTargetCpuCopyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOpsRhiTestPrivate;
	if (!CanRunOpsRhiTest(*this, TEXT("Ops.ExternalTargetCpuCopy")))
	{
		return false;
	}

	FTDFluidOpsRhiTestWorld TestWorld;
	UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.World->GetSubsystem<UTDFluidLiveSubsystem>();
	UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Mud);
	ATDFluidLiveActor* LiveActor = FluidSubsystem ? FluidSubsystem->SpawnPooledFluidActor(Preset, FVector::ZeroVector, 0.0f) : nullptr;
	UTDFluidLiveComponent* Component = LiveActor ? LiveActor->LiveComponent.Get() : nullptr;
	if (!TestNotNull(TEXT("Spawned a mud component"), Component) || !TestFalse(TEXT("Mud runs on the CPU solver"), Component->IsUsingGpuSolver()))
	{
		return false;
	}

	const int32 Resolution = Component->GetSimulationResolution();
	TStrongObjectPtr<UTextureRenderTarget2D> ExternalTarget(CreateExternalTarget(Resolution, Component->IsAreaWrapped() ? TA_Wrap : TA_Clamp));
	Component->ExternalOutputTargets.DensityPressureTarget = ExternalTarget.Get();
	for (int32 StepIndex = 0; StepIndex < OpsRhiCpuStepCount; ++StepIndex)
	{
		QueueOpsStamp(*Component, StepIndex);
		TestWorld.StepWorldAndWaitForGpu();
	}

	TArray<FFloat16Color> ExternalPixels;
	if (!TestTrue(TEXT("Read the external target"), ReadRenderTargetPixels(ExternalTarget.Get(), ExternalPixels)))
	{
		return false;
	}

	const TArray<FFloat16Color>& PackedPixels = Component->GetPackedDensityPressurePixels();
	TestEqual(TEXT("The external target has the solver resolution"), ExternalPixels.Num(), Resolution * Resolution);
	TestTrue(TEXT("The CPU output copy reaches the external target bit for bit"), ArePixelsBitIdentical(ExternalPixels, PackedPixels));
	TestTrue(TEXT("The internal CPU texture stays the material source"), Component->GetDensityPressureTexture() != ExternalTarget.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidOpsExternalTargetRejectsMismatchTest,
	"TDGame.FluidFXRhi.Ops.ExternalTargetRejectsMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidOpsExternalTargetRejectsMismatchTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidOpsRhiTestPrivate;
	if (!CanRunOpsRhiTest(*this, TEXT("Ops.ExternalTargetRejectsMismatch")))
	{
		return false;
	}

	FTDFluidOpsRhiTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnGpuSmokeActor(*TestWorld.World);
	UTDFluidLiveComponent* Component = LiveActor ? LiveActor->LiveComponent.Get() : nullptr;
	if (!TestNotNull(TEXT("Spawned a smoke component"), Component))
	{
		return false;
	}
	if (!Component->IsUsingGpuSolver())
	{
		AddInfo(TEXT("The GPU solver is not available here, so the mismatch fallback check is skipped."));
		return true;
	}

	TStrongObjectPtr<UTextureRenderTarget2D> MismatchedTarget(CreateExternalTarget(OpsRhiMismatchResolution, TA_Clamp));
	AddExpectedMessage(TEXT("does not match the fluid output"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	Component->ExternalOutputTargets.DensityPressureTarget = MismatchedTarget.Get();
	for (int32 StepIndex = 0; StepIndex < OpsRhiMismatchStepCount; ++StepIndex)
	{
		QueueOpsStamp(*Component, StepIndex);
		TestWorld.StepWorldAndWaitForGpu();
	}

	const UTextureRenderTarget2D* OutputTarget = Cast<UTextureRenderTarget2D>(Component->GetDensityPressureTexture());
	TestNotNull(TEXT("The component still has a density-pressure target"), OutputTarget);
	TestTrue(TEXT("A mismatched external target falls back to the internal target"), OutputTarget && OutputTarget != MismatchedTarget.Get());
	TestEqual(TEXT("The fallback target has the solver resolution"), OutputTarget ? OutputTarget->SizeX : 0, OpsRhiGpuResolution);
	return true;
}

#endif
