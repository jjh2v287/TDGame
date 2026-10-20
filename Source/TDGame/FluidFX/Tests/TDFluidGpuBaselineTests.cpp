#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UnrealClient.h"

namespace TDFluidGpuBaselineTestPrivate
{
	constexpr float BaselineStepSeconds = 1.0f / 60.0f;
	constexpr int32 BaselineStepCount = 60;
	constexpr int32 BaselineResolution = 128;
	const FVector BaselineAnchorLocation(310.0, -145.0, 0.0);

	FString GetBaselinePath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX"), TEXT("fx8b-gpu-baseline.json"));
	}

	FTDFluidSimulationConfig MakeBaselineConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.SolverBackend = ETDFluidSolverBackend::Gpu;
		Config.GridResolution = BaselineResolution;
		Config.SimExtentCm = FVector2D(700.0, 700.0);
		Config.VelocityDamping = 0.08f;
		Config.DensityDissipation = 0.2f;
		Config.VorticityStrength = 1.2f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 70.0f;
		Config.FlowNoiseScaleCm = 220.0f;
		Config.BuoyancyCmPerSec2 = 150.0f;
		return Config;
	}

	uint32 ReadRenderTargetChecksum(UTexture* Texture)
	{
		UTextureRenderTarget2D* Target = Cast<UTextureRenderTarget2D>(Texture);
		FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		TArray<FFloat16Color> Pixels;
		if (!Resource || !static_cast<FRenderTarget*>(Resource)->ReadFloat16Pixels(Pixels))
		{
			return 0u;
		}
		return FCrc::MemCrc32(Pixels.GetData(), Pixels.Num() * sizeof(FFloat16Color));
	}

	struct FTDGpuBaselineWorld
	{
		FTDGpuBaselineWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidGpuBaselineWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDGpuBaselineWorld()
		{
			if (!World)
			{
				return;
			}

			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			FlushRenderingCommands();
		}

		void StepWorld(float DeltaSeconds)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
			ENQUEUE_RENDER_COMMAND(TDFluidGpuBaselineWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuDisabledFeaturesBaselineTest,
	"TDGame.FluidFXRhi.Heat.DisabledKeepsGpuOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuDisabledFeaturesBaselineTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuBaselineTestPrivate;
	if (GUsingNullRHI)
	{
		AddError(TEXT("TDGame.FluidFXRhi.Heat.DisabledKeepsGpuOutputs needs a rendering RHI."));
		return false;
	}

	FTDGpuBaselineWorld TestWorld;
	const FTransform SpawnTransform(FRotator::ZeroRotator, BaselineAnchorLocation);
	ATDFluidLiveActor* LiveActor = TestWorld.World->SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
	if (!TestNotNull(TEXT("Spawned the baseline live actor"), LiveActor))
	{
		return false;
	}
	LiveActor->LiveComponent->SimulationConfig = MakeBaselineConfig();
	LiveActor->FinishSpawning(SpawnTransform);

	for (int32 StepIndex = 0; StepIndex < BaselineStepCount; ++StepIndex)
	{
		const double Angle = 0.17 * StepIndex;
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = BaselineAnchorLocation + FVector(FMath::Cos(Angle) * 140.0, FMath::Sin(Angle) * 140.0, 0.0);
		Stamp.WorldVelocity = FVector(-FMath::Sin(Angle) * 300.0, FMath::Cos(Angle) * 300.0, 0.0);
		Stamp.RadiusCm = 45.0f;
		Stamp.DensityStrength = 0.9f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
		LiveActor->LiveComponent->QueueBrushStamp(Stamp);
		TestWorld.StepWorld(BaselineStepSeconds);
	}

	if (!TestTrue(TEXT("The baseline actor uses the GPU solver"), LiveActor->LiveComponent->IsUsingGpuSolver()))
	{
		return false;
	}

	const uint32 DensityChecksum = ReadRenderTargetChecksum(LiveActor->LiveComponent->GetDensityPressureTexture());
	const uint32 VelocityChecksum = ReadRenderTargetChecksum(LiveActor->LiveComponent->GetVelocityTexture());
	const FString Line = FString::Printf(TEXT("TDFluidGpuBaseline: density=0x%08X velocity=0x%08X adapter=%s"), DensityChecksum, VelocityChecksum, *GRHIAdapterName);
	AddInfo(Line);

	const FString BaselinePath = GetBaselinePath();
	FString BaselineText;
	if (!FFileHelper::LoadFileToString(BaselineText, *BaselinePath))
	{
		TSharedRef<FJsonObject> BaselineObject = MakeShared<FJsonObject>();
		BaselineObject->SetNumberField(TEXT("density_checksum"), DensityChecksum);
		BaselineObject->SetNumberField(TEXT("velocity_checksum"), VelocityChecksum);
		BaselineObject->SetStringField(TEXT("adapter"), GRHIAdapterName);
		FString OutputText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputText);
		FJsonSerializer::Serialize(BaselineObject, Writer);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(BaselinePath), true);
		TestTrue(TEXT("Recorded the GPU baseline before FX-8b integration"), FFileHelper::SaveStringToFile(OutputText, *BaselinePath));
		AddInfo(FString::Printf(TEXT("TDFluidGpuBaseline: recorded %s"), *BaselinePath));
		return true;
	}

	TSharedPtr<FJsonObject> BaselineObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BaselineText);
	if (!TestTrue(TEXT("Parsed the GPU baseline"), FJsonSerializer::Deserialize(Reader, BaselineObject) && BaselineObject.IsValid()))
	{
		return false;
	}

	TestEqual(TEXT("Density output matches the pre-integration GPU baseline on this machine"), DensityChecksum, static_cast<uint32>(BaselineObject->GetNumberField(TEXT("density_checksum"))));
	TestEqual(TEXT("Velocity output matches the pre-integration GPU baseline on this machine"), VelocityChecksum, static_cast<uint32>(BaselineObject->GetNumberField(TEXT("velocity_checksum"))));
	return true;
}

#endif
