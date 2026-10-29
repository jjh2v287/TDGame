#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidFieldInputComponent.h"
#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "TDFluidGpuBatch.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace TDFluidFieldInputCpuTestPrivate
{
	constexpr float FieldInputCpuStepSeconds = 0.05f;
	constexpr int32 FieldInputCpuStepCount = 3;

	struct FTDFluidFieldInputTestWorld
	{
		FTDFluidFieldInputTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidFieldInputTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidFieldInputTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		UWorld* World = nullptr;
	};

	ATDFluidLiveActor* SpawnFieldInputLiveActor(UWorld* World)
	{
		ATDFluidLiveActor* LiveActor = World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (!LiveActor)
		{
			return nullptr;
		}

		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(World, ETDFluidPresetKind::Smoke);
		Preset->bOverridePresetDefaults = true;
		Preset->SimulationConfig.GridResolution = 64;
		Preset->SimulationConfig.SimExtentCm = FVector2D(2000.0f, 2000.0f);
		LiveActor->InitializeFromPreset(Preset, 0.0f);
		return LiveActor;
	}

	TArray<FFloat16Color> StepFieldInputLiveActor(ATDFluidLiveActor& LiveActor)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector::ZeroVector;
		Stamp.WorldVelocity = FVector(150.0, 0.0, 0.0);
		Stamp.RadiusCm = 120.0f;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 1.0f;
		for (int32 StepIndex = 0; StepIndex < FieldInputCpuStepCount; ++StepIndex)
		{
			LiveActor.LiveComponent->QueueBrushStamp(Stamp);
			LiveActor.LiveComponent->AdvanceSimulationStep(FieldInputCpuStepSeconds, FVector2D::ZeroVector, false);
		}
		return LiveActor.LiveComponent->GetPackedDensityPressurePixels();
	}
}

namespace TDFluidFieldInputTestPrivate
{
	constexpr float AdditiveStepSeconds = 1.0f / 60.0f;
	constexpr int32 AdditiveResolution = 64;
	constexpr int32 AdditiveStepCount = 10;
	constexpr float AdditiveStampStrength = 3.0f;
	constexpr double AdditiveTolerance = 2.0e-3;

	uint64 AdditiveNextActorKey = 0x41444400ull;

	FTDFluidSimulationConfig MakeAdditiveConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = AdditiveResolution;
		Config.bEnableWaterLayer = false;
		Config.DensityDissipation = 0.0f;
		Config.VelocityDamping = 0.0f;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientDensity = 0.0f;
		Config.AmbientRefillPerSec = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.BuoyancyCmPerSec2 = 0.0f;
		Config.VortexTangentialAccelerationCmPerSec2 = 0.0f;
		Config.VortexRadialDriftCmPerSec = 0.0f;
		return Config;
	}

	TArray<FTDFluidBrushStamp> MakeAdditiveStamps(bool bAddsDensity)
	{
		TArray<FTDFluidBrushStamp> Stamps;
		for (const double LocationXCm : { -300.0, 300.0 })
		{
			FTDFluidBrushStamp& Stamp = Stamps.AddDefaulted_GetRef();
			Stamp.WorldLocation = FVector(LocationXCm, 0.0, 0.0);
			Stamp.RadiusCm = 250.0f;
			Stamp.DensityStrength = AdditiveStampStrength;
			Stamp.VelocityStrength = 0.0f;
			Stamp.Channel = LocationXCm < 0.0 ? ETDFluidBrushChannel::ChannelR_Mud : ETDFluidBrushChannel::ChannelG_Blood;
			Stamp.bAddsDensity = bAddsDensity;
		}
		return Stamps;
	}

	UTextureRenderTarget2D* CreateAdditiveTarget(int32 Resolution)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_RGBA16f;
		Target->ClearColor = FLinearColor::Transparent;
		Target->bAutoGenerateMips = false;
		Target->bSupportsUAV = true;
		Target->bCanCreateUAV = true;
		Target->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, true);
		Target->UpdateResourceImmediate(true);
		return Target;
	}

	struct FTDAdditiveGpuArm
	{
		FTDAdditiveGpuArm() = default;
		FTDAdditiveGpuArm(const FTDAdditiveGpuArm&) = delete;
		FTDAdditiveGpuArm& operator=(const FTDAdditiveGpuArm&) = delete;

		~FTDAdditiveGpuArm()
		{
			ENQUEUE_RENDER_COMMAND(TDFluidAdditiveRhiRelease)([State = MoveTemp(RenderState)](FRHICommandListImmediate&) mutable
			{
				State.Reset();
			});
			FlushRenderingCommands();
		}

		void Initialize(const FTDFluidSimulationConfig& Config)
		{
			FTDFluidSimulationConfig GpuConfig = Config;
			GpuConfig.SolverBackend = ETDFluidSolverBackend::Gpu;
			Proxy.Initialize(GpuConfig, GpuConfig.GridResolution, FVector2D::ZeroVector);
			Proxy.AssignActorKey(++AdditiveNextActorKey);
			DensityTarget = TStrongObjectPtr<UTextureRenderTarget2D>(CreateAdditiveTarget(Proxy.GetResolution()));
			VelocityTarget = TStrongObjectPtr<UTextureRenderTarget2D>(CreateAdditiveTarget(Proxy.GetResolution()));
		}

		bool Step(const TArray<FTDFluidBrushStamp>& Stamps)
		{
			Proxy.PrepareStep(AdditiveStepSeconds, TConstArrayView<FTDFluidBrushStamp>(Stamps), FVector2D::ZeroVector, false);
			FTDFluidGpuFrameInputs FrameInputs;
			FrameInputs.FrameNumber = ++FrameNumber;
			FTDFluidGpuActorInput& ActorInput = FrameInputs.Actors.AddDefaulted_GetRef();
			if (!Proxy.GatherStep(FrameInputs.FrameNumber, 0, DensityTarget->GameThread_GetRenderTargetResource(), VelocityTarget->GameThread_GetRenderTargetResource(), ActorInput))
			{
				return false;
			}

			ENQUEUE_RENDER_COMMAND(TDFluidAdditiveRhiStep)([State = RenderState, SubmittedInputs = MoveTemp(FrameInputs)](FRHICommandListImmediate& RHICmdList)
			{
				State->ExecuteFrame(RHICmdList, SubmittedInputs);
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
			return true;
		}

		bool ReadDensity(TArray<FFloat16Color>& OutPixels)
		{
			FRenderTarget* Target = DensityTarget->GameThread_GetRenderTargetResource();
			return Target && Target->ReadFloat16Pixels(OutPixels) && !OutPixels.IsEmpty();
		}

		FTDFluidGpuSolverProxy Proxy;
		TSharedPtr<FTDFluidGpuSolverRenderState, ESPMode::ThreadSafe> RenderState = MakeShared<FTDFluidGpuSolverRenderState, ESPMode::ThreadSafe>();
		TStrongObjectPtr<UTextureRenderTarget2D> DensityTarget;
		TStrongObjectPtr<UTextureRenderTarget2D> VelocityTarget;
		uint32 FrameNumber = 0;
	};

	TArray<FFloat16Color> RunAdditiveCpu(bool bAddsDensity)
	{
		FTDFluidSimulationConfig Config = MakeAdditiveConfig();
		Config.SolverBackend = ETDFluidSolverBackend::Cpu;
		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, FVector2D::ZeroVector);
		const TArray<FTDFluidBrushStamp> Stamps = MakeAdditiveStamps(bAddsDensity);
		for (int32 StepIndex = 0; StepIndex < AdditiveStepCount; ++StepIndex)
		{
			Solver.ApplyBrushStamps(Stamps, AdditiveStepSeconds);
			Solver.StepSimulation(AdditiveStepSeconds);
		}
		TArray<FFloat16Color> Density;
		TArray<FFloat16Color> Velocity;
		Solver.PackOutputPixels(Density, Velocity);
		return Density;
	}

	double MeasurePeak(const TArray<FFloat16Color>& Pixels, bool bSelective)
	{
		double Peak = 0.0;
		for (const FFloat16Color& Pixel : Pixels)
		{
			const FLinearColor Value = Pixel.GetFloats();
			Peak = FMath::Max<double>(Peak, bSelective ? Value.B : Value.R);
		}
		return Peak;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFieldInputAdditiveStampsRhiTest,
	"TDGame.FluidFXRhi.Input.AdditiveStampsMatchCpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidFieldInputAdditiveStampsRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFieldInputTestPrivate;
	if (GUsingNullRHI || !FApp::CanEverRender())
	{
		AddError(TEXT("TDGame.FluidFXRhi.Input.AdditiveStampsMatchCpu needs a rendering RHI."));
		return false;
	}

	const TArray<FFloat16Color> CpuPixels = RunAdditiveCpu(true);
	const TArray<FFloat16Color> CpuMaxPixels = RunAdditiveCpu(false);
	FTDAdditiveGpuArm GpuArm;
	GpuArm.Initialize(MakeAdditiveConfig());
	const TArray<FTDFluidBrushStamp> Stamps = MakeAdditiveStamps(true);
	for (int32 StepIndex = 0; StepIndex < AdditiveStepCount; ++StepIndex)
	{
		if (!TestTrue(*FString::Printf(TEXT("GPU step %d gathered"), StepIndex), GpuArm.Step(Stamps)))
		{
			return false;
		}
	}

	TArray<FFloat16Color> GpuPixels;
	if (!TestTrue(TEXT("Read the GPU density target"), GpuArm.ReadDensity(GpuPixels)) || !TestEqual(TEXT("CPU and GPU hold the same cell count"), GpuPixels.Num(), CpuPixels.Num()))
	{
		return false;
	}

	double MaximumDifference = 0.0;
	for (int32 PixelIndex = 0; PixelIndex < CpuPixels.Num(); ++PixelIndex)
	{
		const FLinearColor Cpu = CpuPixels[PixelIndex].GetFloats();
		const FLinearColor Gpu = GpuPixels[PixelIndex].GetFloats();
		MaximumDifference = FMath::Max(MaximumDifference, FMath::Max<double>(FMath::Abs(Cpu.R - Gpu.R), FMath::Abs(Cpu.B - Gpu.B)));
	}

	const double ExpectedPeak = AdditiveStampStrength * AdditiveStepSeconds * AdditiveStepCount;
	const double CpuPeak = MeasurePeak(CpuPixels, false);
	const double CpuSelectivePeak = MeasurePeak(CpuPixels, true);
	AddInfo(FString::Printf(TEXT("TDFluidAdditiveStamps: max_diff=%.6f cpu_peak=%.4f gpu_peak=%.4f selective_peak=%.4f max_path_peak=%.4f expected=%.4f"),
		MaximumDifference, CpuPeak, MeasurePeak(GpuPixels, false), CpuSelectivePeak, MeasurePeak(CpuMaxPixels, false), ExpectedPeak));
	TestTrue(*FString::Printf(TEXT("CPU and GPU additive density differ by at most 2e-3 (%.6f)"), MaximumDifference), MaximumDifference <= AdditiveTolerance);
	TestTrue(TEXT("Additive peak grows by strength times dt per step"), FMath::Abs(CpuPeak - ExpectedPeak) <= 0.05);
	TestTrue(TEXT("Additive selective density grows the same way"), FMath::Abs(CpuSelectivePeak - ExpectedPeak) <= 0.05);
	TestTrue(TEXT("The max path reaches the ceiling instead"), MeasurePeak(CpuMaxPixels, false) >= 0.99);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFieldInputCpuIgnoresTest,
	"TDGame.FluidFX.Input.CpuIgnoresFieldInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFieldInputCpuIgnoresTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFieldInputCpuTestPrivate;
	FTDFluidFieldInputTestWorld TestWorld;
	ATDFluidLiveActor* BaselineActor = SpawnFieldInputLiveActor(TestWorld.World);
	ATDFluidLiveActor* FieldActor = SpawnFieldInputLiveActor(TestWorld.World);
	if (!TestNotNull(TEXT("Baseline live actor"), BaselineActor) || !TestNotNull(TEXT("Field live actor"), FieldActor))
	{
		return false;
	}

	FTDFluidFieldInputSettings Settings = FieldActor->LiveComponent->GetFieldInputSettings();
	Settings.bAcceptFieldInputs = true;
	FieldActor->LiveComponent->SetFieldInputSettingsForTests(Settings);
	TArray<FTDFluidFieldInputPlacement> Placements;
	FTDFluidFieldInputPlacement& Placement = Placements.AddDefaulted_GetRef();
	Placement.Texture = UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8);
	Placement.SizeCm = FVector2D(400.0, 400.0);
	FieldActor->LiveComponent->SetFieldInputPlacements(MoveTemp(Placements));
	AddExpectedMessage(TEXT("ignores field inputs because field inputs need the GPU fluid solver"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

	const TArray<FFloat16Color> BaselinePixels = StepFieldInputLiveActor(*BaselineActor);
	const TArray<FFloat16Color> FieldPixels = StepFieldInputLiveActor(*FieldActor);
	TestFalse(TEXT("Both live actors run the CPU solver"), BaselineActor->LiveComponent->IsUsingGpuSolver() || FieldActor->LiveComponent->IsUsingGpuSolver());
	TestEqual(TEXT("The CPU solver ignores the one field input"), FieldActor->LiveComponent->GetIgnoredFieldInputCount(), 1);
	TestEqual(TEXT("A live actor without field inputs ignores none"), BaselineActor->LiveComponent->GetIgnoredFieldInputCount(), 0);
	TestTrue(TEXT("The stamp painted density"), BaselinePixels.Num() > 0);
	TestTrue(TEXT("The ignored field input leaves the CPU output bit-identical"),
		BaselinePixels.Num() == FieldPixels.Num() && FMemory::Memcmp(BaselinePixels.GetData(), FieldPixels.GetData(), BaselinePixels.Num() * BaselinePixels.GetTypeSize()) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFieldInputStageAssetsLoadRhiTest,
	"TDGame.FluidFXRhi.Input.StageAssetsLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidFieldInputStageAssetsLoadRhiTest::RunTest(const FString& Parameters)
{
	struct FTDFieldInputStageTexture
	{
		const TCHAR* ObjectPath;
		TextureCompressionSettings Compression;
	};
	const FTDFieldInputStageTexture StageTextures[] = {
		{ TEXT("/Game/FluidFX/Textures/Input/T_TD_FluidFieldWindSwirl.T_TD_FluidFieldWindSwirl"), TC_VectorDisplacementmap },
		{ TEXT("/Game/FluidFX/Textures/Input/T_TD_FluidFieldDensityRings.T_TD_FluidFieldDensityRings"), TC_VectorDisplacementmap },
		{ TEXT("/Game/FluidFX/Textures/Input/T_TD_FluidFieldMaskMaze.T_TD_FluidFieldMaskMaze"), TC_VectorDisplacementmap },
		{ TEXT("/Game/FluidFX/Textures/Input/T_TD_FluidMeshField_Cube.T_TD_FluidMeshField_Cube"), TC_HDR },
		{ TEXT("/Game/FluidFX/Textures/Input/T_TD_FluidMeshField_Cylinder.T_TD_FluidMeshField_Cylinder"), TC_HDR },
	};
	for (const FTDFieldInputStageTexture& StageTexture : StageTextures)
	{
		const UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, StageTexture.ObjectPath);
		if (!TestNotNull(*FString::Printf(TEXT("Stage texture %s loads"), StageTexture.ObjectPath), Texture))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s compression"), StageTexture.ObjectPath), static_cast<int32>(Texture->CompressionSettings), static_cast<int32>(StageTexture.Compression));
		TestFalse(*FString::Printf(TEXT("%s is linear"), StageTexture.ObjectPath), static_cast<bool>(Texture->SRGB));
	}

	const TPair<const TCHAR*, ETDFluidPresetKind> StagePresets[] = {
		{ TEXT("/Game/FluidFX/Presets/DA_TDFluid_InputBrushBench.DA_TDFluid_InputBrushBench"), ETDFluidPresetKind::InputBrushBench },
		{ TEXT("/Game/FluidFX/Presets/DA_TDFluid_InputFieldBench.DA_TDFluid_InputFieldBench"), ETDFluidPresetKind::InputFieldBench },
	};
	for (const TPair<const TCHAR*, ETDFluidPresetKind>& StagePreset : StagePresets)
	{
		const UTDFluidPresetAsset* Preset = LoadObject<UTDFluidPresetAsset>(nullptr, StagePreset.Key);
		if (TestNotNull(*FString::Printf(TEXT("Stage preset %s loads"), StagePreset.Key), Preset))
		{
			TestTrue(*FString::Printf(TEXT("%s has its preset kind"), StagePreset.Key), Preset->PresetKind == StagePreset.Value);
		}
	}
	return true;
}

#endif
