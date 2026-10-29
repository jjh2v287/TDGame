#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidFoliageBendLayer.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace TDFluidFoliageBendRhiTestsPrivate
{
	const TCHAR* FlowCopyMaterialPath = TEXT("/Game/FluidFX/Materials/M_TD_FluidFoliageFlowCopy.M_TD_FluidFoliageFlowCopy");
	constexpr int32 SyntheticResolution = 64;
	constexpr double SyntheticExtentCm = 250.0;
	constexpr float StepSeconds = 1.0f / 60.0f;
	constexpr int32 ParityStepCount = 60;
	constexpr int32 ParityShiftStep = 30;
	constexpr int32 ParityResetStep = 45;
	constexpr int32 ParityShiftCellsX = 3;
	constexpr int32 ParityShiftCellsY = -2;
	constexpr float ParityTolerance = 1.0e-3f;
	constexpr float PreservedRed = 0.25f;
	constexpr float PreservedGreen = -0.5f;
	constexpr int32 AuxOnlyStepCount = 10;
	constexpr int32 LiveStepCount = 10;
	constexpr int32 WaterStepCount = 6;
	constexpr float LiveStampSpeedCmPerSec = 300.0f;
	constexpr float LiveStampRadiusCm = 60.0f;
	constexpr float LiveBendThreshold = 0.05f;
	constexpr float OutputSpeedReferenceCmPerSec = 600.0f;
	constexpr float BendMaxDegrees = 70.0f;
	constexpr float BendSpeedReferenceCmPerSec = 300.0f;
	constexpr float BendResponse = 1.0f;
	constexpr float CopyTolerance = 0.01f;
	constexpr int32 CostFrameCount = 120;
	constexpr int32 CostCpuResolution = 256;
	constexpr int32 CostGpuResolution = 512;
	constexpr double CostExtentCm = 1000.0;

	bool CanRunFoliageBendRhiTest(FAutomationTestBase& Test, const FString& CaseLabel)
	{
		if (!GUsingNullRHI && FApp::CanEverRender())
		{
			return true;
		}
		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), *CaseLabel));
		return false;
	}

	void WaitForGpu()
	{
		ENQUEUE_RENDER_COMMAND(TDFluidFoliageBendRhiWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.SubmitAndBlockUntilGPUIdle();
		});
		FlushRenderingCommands();
	}

	struct FTDFluidFoliageBendRhiTestWorld
	{
		FTDFluidFoliageBendRhiTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidFoliageBendRhiWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidFoliageBendRhiTestWorld()
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

		void StepWorld(float DeltaSeconds)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
			WaitForGpu();
		}

		UWorld* World = nullptr;
	};

	struct FTDFluidFoliageBendSyntheticInputs
	{
		TArray<FFloat16Color> VelocityPixels;
		TArray<FFloat16Color> DensityPixels;
		TArray<FFloat16Color> WaterPixels;
	};

	struct FTDFluidFoliageBendParityResult
	{
		bool bHasPixels = false;
		bool bHasGpuOutput = false;
		float LargestDifference = 0.0f;
		float LargestBend = 0.0f;
		int32 RedGreenMismatchCount = 0;
	};

	FTDFluidFoliageBendSyntheticInputs MakeSyntheticInputs(int32 Resolution)
	{
		FTDFluidFoliageBendSyntheticInputs Inputs;
		const int32 CellCount = Resolution * Resolution;
		Inputs.VelocityPixels.SetNumUninitialized(CellCount);
		Inputs.DensityPixels.SetNumUninitialized(CellCount);
		Inputs.WaterPixels.SetNumUninitialized(CellCount);
		const float HalfResolution = 0.5f * Resolution;
		const float DensityRadiusSquared = 0.02f * Resolution * Resolution;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const int32 CellIndex = CellY * Resolution + CellX;
				const float OffsetX = (CellX + 0.5f - HalfResolution) / HalfResolution;
				const float OffsetY = (CellY + 0.5f - HalfResolution) / HalfResolution;
				const FVector2f Velocity(-0.6f * OffsetY, 0.6f * OffsetX);
				const float DensityDistanceSquared = FMath::Square(CellX - 0.3f * Resolution) + FMath::Square(CellY - 0.6f * Resolution);
				const float Density = FMath::Exp(-DensityDistanceSquared / DensityRadiusSquared);
				const float Ripple = 0.3f * FMath::Sin(0.4f * CellX) * FMath::Cos(0.3f * CellY);
				Inputs.VelocityPixels[CellIndex] = FFloat16Color(FLinearColor(0.5f + 0.5f * Velocity.X, 0.5f + 0.5f * Velocity.Y, 0.0f, 1.0f));
				Inputs.DensityPixels[CellIndex] = FFloat16Color(FLinearColor(Density, 0.0f, 0.0f, 1.0f));
				Inputs.WaterPixels[CellIndex] = FFloat16Color(FLinearColor(0.0f, 0.0f, 0.5f + 0.5f * Ripple, 1.0f));
			}
		}
		return Inputs;
	}

	FTDFluidSimulationConfig MakeSyntheticConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::SimplePainter;
		Config.bWritesPainterVelocity = true;
		Config.bEnableBendVectorChannel = true;
		Config.BendVectorRiseSeconds = 0.1f;
		Config.BendVectorRecoverySeconds = 0.8f;
		Config.BendFromPainterDensity = 0.7f;
		Config.BendFromRippleSlope = 0.15f;
		return Config;
	}

	UTexture2D* CreatePixelTexture(const TArray<FFloat16Color>& Pixels, int32 Resolution)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Resolution, Resolution, PF_FloatRGBA);
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}

		Texture->SRGB = false;
		Texture->Filter = TF_Nearest;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* MipPixels = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(MipPixels, Pixels.GetData(), Pixels.Num() * sizeof(FFloat16Color));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	UTextureRenderTarget2D* CreateAuxTarget(int32 Resolution)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_RGBA16f;
		Target->ClearColor = FLinearColor(PreservedRed, PreservedGreen, 0.0f, 0.0f);
		Target->bAutoGenerateMips = false;
		Target->bSupportsUAV = true;
		Target->bCanCreateUAV = true;
		Target->Filter = TF_Nearest;
		Target->AddressX = TA_Clamp;
		Target->AddressY = TA_Clamp;
		Target->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, true);
		Target->UpdateResourceImmediate(true);
		return Target;
	}

	bool ReadTargetPixels(UTexture* Texture, TArray<FFloat16Color>& OutPixels)
	{
		OutPixels.Reset();
		UTextureRenderTarget2D* Target = Cast<UTextureRenderTarget2D>(Texture);
		FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		if (!Resource)
		{
			return false;
		}

		FRenderTarget* ReadableTarget = Resource;
		return ReadableTarget->ReadFloat16Pixels(OutPixels) && OutPixels.Num() == Target->SizeX * Target->SizeY;
	}

	float ComputeLargestPackedBend(const TArray<FFloat16Color>& Pixels)
	{
		float LargestBend = 0.0f;
		for (const FFloat16Color& Pixel : Pixels)
		{
			LargestBend = FMath::Max(LargestBend, FVector2f(Pixel.B.GetFloat(), Pixel.A.GetFloat()).Size());
		}
		return LargestBend;
	}

	FTDFluidFoliageBendParityResult RunSyntheticParity(bool bPreservesAuxRedGreen, int32 StepCount)
	{
		FTDFluidFoliageBendParityResult Result;
		const int32 Resolution = SyntheticResolution;
		const FVector2D CellSizeCm(SyntheticExtentCm / Resolution, SyntheticExtentCm / Resolution);
		const FTDFluidFoliageBendSyntheticInputs Inputs = MakeSyntheticInputs(Resolution);
		TStrongObjectPtr<UTexture2D> VelocityTexture(CreatePixelTexture(Inputs.VelocityPixels, Resolution));
		TStrongObjectPtr<UTexture2D> DensityTexture(CreatePixelTexture(Inputs.DensityPixels, Resolution));
		TStrongObjectPtr<UTexture2D> WaterTexture(CreatePixelTexture(Inputs.WaterPixels, Resolution));
		TStrongObjectPtr<UTextureRenderTarget2D> AuxTarget(CreateAuxTarget(Resolution));
		FlushRenderingCommands();
		if (!VelocityTexture.IsValid() || !DensityTexture.IsValid() || !WaterTexture.IsValid() || !AuxTarget.IsValid())
		{
			return Result;
		}

		const FTDFluidFoliageBendParameters Parameters = FTDFluidFoliageBendLayer::MakeParameters(MakeSyntheticConfig(), StepSeconds, CellSizeCm, true);
		FTDFluidFoliageBendLayer CpuLayer;
		FTDFluidFoliageBendLayer GpuLayer;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			const bool bHasShifted = StepIndex >= ParityShiftStep;
			const FVector2D CenterWorld(bHasShifted ? ParityShiftCellsX * CellSizeCm.X : 0.0, bHasShifted ? ParityShiftCellsY * CellSizeCm.Y : 0.0);
			if (StepIndex == ParityResetStep)
			{
				CpuLayer.RequestReset();
				GpuLayer.RequestReset();
			}
			CpuLayer.AdvanceCpu(Inputs.VelocityPixels, Inputs.DensityPixels, Inputs.WaterPixels, Resolution, CenterWorld, CellSizeCm, Parameters);
			GpuLayer.SubmitGpu(VelocityTexture.Get(), DensityTexture.Get(), WaterTexture.Get(), AuxTarget.Get(), bPreservesAuxRedGreen, Resolution, CenterWorld, CellSizeCm, Parameters);
		}
		WaitForGpu();
		Result.bHasGpuOutput = GpuLayer.HasOutput();

		TArray<FFloat16Color> AuxPixels;
		const TArray<FVector2f>& CpuBend = CpuLayer.GetCpuBendState();
		Result.bHasPixels = ReadTargetPixels(AuxTarget.Get(), AuxPixels) && CpuBend.Num() == AuxPixels.Num();
		if (!Result.bHasPixels)
		{
			return Result;
		}

		const FFloat16 ExpectedRed(bPreservesAuxRedGreen ? PreservedRed : 0.0f);
		const FFloat16 ExpectedGreen(bPreservesAuxRedGreen ? PreservedGreen : 0.0f);
		for (int32 CellIndex = 0; CellIndex < AuxPixels.Num(); ++CellIndex)
		{
			const FFloat16Color& Pixel = AuxPixels[CellIndex];
			const FVector2f GpuBend(Pixel.B.GetFloat(), Pixel.A.GetFloat());
			Result.LargestDifference = FMath::Max(Result.LargestDifference, (GpuBend - CpuBend[CellIndex]).Size());
			Result.LargestBend = FMath::Max(Result.LargestBend, GpuBend.Size());
			Result.RedGreenMismatchCount += Pixel.R.Encoded != ExpectedRed.Encoded || Pixel.G.Encoded != ExpectedGreen.Encoded ? 1 : 0;
		}
		return Result;
	}

	ATDFluidLiveActor* SpawnFoliageLiveActor(UWorld& TargetWorld, const FTDFluidSimulationConfig& Config)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, FVector::ZeroVector);
		ATDFluidLiveActor* LiveActor = TargetWorld.SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!LiveActor || !LiveActor->LiveComponent)
		{
			return nullptr;
		}

		LiveActor->LiveComponent->SimulationConfig = Config;
		LiveActor->FinishSpawning(SpawnTransform);
		return LiveActor;
	}

	FTDFluidBrushStamp MakeWalkStamp()
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = FVector::ZeroVector;
		Stamp.WorldVelocity = FVector(LiveStampSpeedCmPerSec, 0.0, 0.0);
		Stamp.RadiusCm = LiveStampRadiusCm;
		Stamp.DensityStrength = 1.0f;
		Stamp.VelocityStrength = 1.0f;
		return Stamp;
	}

	FLinearColor MakePackedVelocity(int32 PixelX, int32 PixelY)
	{
		return FLinearColor((PixelX + 0.5f) / SyntheticResolution, (PixelY + 0.5f) / SyntheticResolution, 0.0f, 1.0f);
	}

	FLinearColor ComputeExpectedFlow(const FLinearColor& Packed)
	{
		const FVector2f Velocity((2.0f * Packed.R - 1.0f) * OutputSpeedReferenceCmPerSec, (2.0f * Packed.G - 1.0f) * OutputSpeedReferenceCmPerSec);
		const float Speed = Velocity.Size();
		const float Magnitude = FMath::Pow(FMath::Clamp(Speed / BendSpeedReferenceCmPerSec, 0.0f, 1.0f), BendResponse);
		const float Degrees = FMath::Clamp(BendMaxDegrees, 0.0f, 85.0f) * Magnitude;
		const FVector2f Direction = Velocity / FMath::Max(Speed, 0.001f);
		return FLinearColor(0.5f + 0.5f * Direction.X * Degrees / 90.0f, 0.5f + 0.5f * Direction.Y * Degrees / 90.0f, Degrees / 90.0f, 1.0f);
	}

	double ReadPercentile(TArray<double> Values, double Fraction)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		Values.Sort();
		const int32 Index = FMath::Clamp(FMath::FloorToInt32(Fraction * (Values.Num() - 1) + 0.5), 0, Values.Num() - 1);
		return Values[Index];
	}

	bool SaveFoliageBendCostRecord(const TSharedRef<FJsonObject>& Report, FString& OutPath)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentOps"), FDateTime::Now().ToString(TEXT("%Y%m%d")), TEXT("fx13c"));
		OutPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("foliage_bend_cost.json")));
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		return FJsonSerializer::Serialize(Report, Writer) && FFileHelper::SaveStringToFile(Text, *OutPath);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendRhiGpuMatchesCpuTest,
	"TDGame.FluidFXRhi.FoliageBend.GpuMatchesCpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendRhiGpuMatchesCpuTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendRhiTestsPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.FoliageBend.GpuMatchesCpu");
	if (!CanRunFoliageBendRhiTest(*this, CaseLabel))
	{
		return false;
	}

	const FTDFluidFoliageBendParityResult Result = RunSyntheticParity(true, ParityStepCount);
	if (!TestTrue(TEXT("read the GPU aux target and the CPU state at the same size"), Result.bHasPixels))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("%s: largest GPU-CPU bend difference %.6f, largest bend %.4f, red-green mismatches %d"), *CaseLabel, Result.LargestDifference, Result.LargestBend, Result.RedGreenMismatchCount));
	TestTrue(TEXT("GPU layer reports output"), Result.bHasGpuOutput);
	TestTrue(TEXT("synthetic inputs bend the foliage"), Result.LargestBend > 0.01f);
	TestTrue(TEXT("GPU bend matches the CPU bend after shift and reset"), Result.LargestDifference <= ParityTolerance);
	TestEqual(TEXT("preserved red and green bits"), Result.RedGreenMismatchCount, 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendRhiGpuAuxOnlyBendWritesZeroRedGreenTest,
	"TDGame.FluidFXRhi.FoliageBend.GpuAuxOnlyBendWritesZeroRedGreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendRhiGpuAuxOnlyBendWritesZeroRedGreenTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendRhiTestsPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.FoliageBend.GpuAuxOnlyBendWritesZeroRedGreen");
	if (!CanRunFoliageBendRhiTest(*this, CaseLabel))
	{
		return false;
	}

	const FTDFluidFoliageBendParityResult Result = RunSyntheticParity(false, AuxOnlyStepCount);
	if (!TestTrue(TEXT("read the GPU aux target"), Result.bHasPixels))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("%s: largest bend %.4f, red-green not zero %d"), *CaseLabel, Result.LargestBend, Result.RedGreenMismatchCount));
	TestTrue(TEXT("bend-only aux carries the bend"), Result.LargestBend > 0.01f);
	TestEqual(TEXT("bend-only aux writes zero red and green"), Result.RedGreenMismatchCount, 0);
	TestTrue(TEXT("GPU bend still matches the CPU bend"), Result.LargestDifference <= ParityTolerance);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendRhiLiveGpuAuxCarriesBendTest,
	"TDGame.FluidFXRhi.FoliageBend.LiveGpuAuxCarriesBend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendRhiLiveGpuAuxCarriesBendTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendRhiTestsPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.FoliageBend.LiveGpuAuxCarriesBend");
	if (!CanRunFoliageBendRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::FoliageWalk);
	Config.SolverBackend = ETDFluidSolverBackend::Gpu;
	FTDFluidFoliageBendRhiTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = TestWorld.World ? SpawnFoliageLiveActor(*TestWorld.World, Config) : nullptr;
	if (!TestNotNull(TEXT("FoliageWalk live actor spawns"), LiveActor))
	{
		return false;
	}

	UTDFluidLiveComponent& Component = *LiveActor->LiveComponent;
	TStrongObjectPtr<UMaterialInstanceDynamic> ProbeMID(UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), GetTransientPackage()));
	Component.RegisterExternalMaterialTarget(ProbeMID.Get());
	Component.SetSimulationAwake(true);
	for (int32 StepIndex = 0; StepIndex < LiveStepCount; ++StepIndex)
	{
		Component.QueueBrushStamp(MakeWalkStamp());
		TestWorld.StepWorld(StepSeconds);
	}

	if (!TestTrue(TEXT("FoliageWalk selected the GPU solver"), Component.IsUsingGpuSolver()))
	{
		return false;
	}

	TArray<FFloat16Color> AuxPixels;
	if (!TestTrue(TEXT("read the live GPU aux target"), ReadTargetPixels(Component.GetAuxTexture(), AuxPixels)))
	{
		return false;
	}

	const float LargestBend = ComputeLargestPackedBend(AuxPixels);
	float BendActive = 0.0f;
	const bool bHasBendActive = ProbeMID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("AuxFoliageBendActive")), BendActive);
	AddInfo(FString::Printf(TEXT("%s: largest live GPU bend %.4f, AuxFoliageBendActive %.1f"), *CaseLabel, LargestBend, BendActive));
	TestTrue(TEXT("live GPU bend layer has output"), Component.GetFoliageBendLayer().HasOutput());
	TestTrue(TEXT("live GPU aux carries the bend in blue and alpha"), LargestBend > LiveBendThreshold);
	TestTrue(TEXT("registered MID turns AuxFoliageBendActive on"), bHasBendActive && BendActive == 1.0f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendRhiWaterAuxKeepsObstacleDistanceTest,
	"TDGame.FluidFXRhi.FoliageBend.WaterAuxKeepsObstacleDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendRhiWaterAuxKeepsObstacleDistanceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendRhiTestsPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.FoliageBend.WaterAuxKeepsObstacleDistance");
	if (!CanRunFoliageBendRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDFluidSimulationConfig Config = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::ReedShallows);
	Config.SolverBackend = ETDFluidSolverBackend::Cpu;
	Config.bEnableWaterLayer = true;
	Config.bOutputObstacleDistance = true;
	FTDFluidFoliageBendRhiTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = TestWorld.World ? SpawnFoliageLiveActor(*TestWorld.World, Config) : nullptr;
	if (!TestNotNull(TEXT("ReedShallows live actor spawns"), LiveActor))
	{
		return false;
	}

	UTDFluidLiveComponent& Component = *LiveActor->LiveComponent;
	FTDFluidSdfObstacle Obstacle;
	Obstacle.WorldCenter = FVector(80.0, 0.0, 0.0);
	Obstacle.RadiusCm = 40.0f;
	Component.SetSdfObstacles({ Obstacle });
	Component.SetSimulationAwake(true);
	TestTrue(TEXT("water aux and bend aux are both on"), Component.IsObstacleDistanceAuxActive() && Component.IsFoliageBendAuxActive());
	for (int32 StepIndex = 0; StepIndex < WaterStepCount; ++StepIndex)
	{
		Component.QueueBrushStamp(MakeWalkStamp());
		Component.AdvanceSimulationStep(StepSeconds, FVector2D::ZeroVector, false);
	}

	if (!TestFalse(TEXT("ReedShallows runs on the CPU solver"), Component.IsUsingGpuSolver()))
	{
		return false;
	}

	TArray<FFloat16Color> ExpectedAuxPixels;
	Component.GetMutableSolver().PackObstacleDistanceAuxPixels(ExpectedAuxPixels);
	const TArray<FFloat16Color>& AuxPixels = Component.GetPackedAuxPixels();
	if (!TestEqual(TEXT("packed aux matches the obstacle distance size"), AuxPixels.Num(), ExpectedAuxPixels.Num()) || !TestTrue(TEXT("obstacle distance aux is not empty"), !AuxPixels.IsEmpty()))
	{
		return false;
	}

	int32 RedGreenMismatchCount = 0;
	for (int32 CellIndex = 0; CellIndex < AuxPixels.Num(); ++CellIndex)
	{
		RedGreenMismatchCount += AuxPixels[CellIndex].R.Encoded != ExpectedAuxPixels[CellIndex].R.Encoded || AuxPixels[CellIndex].G.Encoded != ExpectedAuxPixels[CellIndex].G.Encoded ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%s: red-green mismatches %d, largest bend %.4f"), *CaseLabel, RedGreenMismatchCount, ComputeLargestPackedBend(AuxPixels)));
	TestEqual(TEXT("bend merge keeps the obstacle distance red and green bits"), RedGreenMismatchCount, 0);
	TestTrue(TEXT("CPU bend layer has output"), Component.GetFoliageBendLayer().HasOutput());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendRhiFlowCopyReadsAuxTest,
	"TDGame.FluidFXRhi.FoliageBendAsset.FlowCopyReadsAux",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendRhiFlowCopyReadsAuxTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendRhiTestsPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.FoliageBendAsset.FlowCopyReadsAux");
	if (!CanRunFoliageBendRhiTest(*this, CaseLabel))
	{
		return false;
	}

	UMaterialInterface* CopyMaterial = LoadObject<UMaterialInterface>(nullptr, FlowCopyMaterialPath);
	if (!TestNotNull(TEXT("flow copy material asset (editor_make_fluid_foliage_material.py)"), CopyMaterial))
	{
		return false;
	}

	const int32 Resolution = SyntheticResolution;
	TArray<FFloat16Color> StillVelocityPixels;
	TArray<FFloat16Color> AuxPixels;
	StillVelocityPixels.Init(FFloat16Color(FLinearColor(0.5f, 0.5f, 0.0f, 1.0f)), Resolution * Resolution);
	AuxPixels.SetNumUninitialized(Resolution * Resolution);
	for (int32 PixelY = 0; PixelY < Resolution; ++PixelY)
	{
		for (int32 PixelX = 0; PixelX < Resolution; ++PixelX)
		{
			const FLinearColor Packed = MakePackedVelocity(PixelX, PixelY);
			AuxPixels[PixelY * Resolution + PixelX] = FFloat16Color(FLinearColor(0.0f, 0.0f, 2.0f * Packed.R - 1.0f, 2.0f * Packed.G - 1.0f));
		}
	}

	FTDFluidFoliageBendRhiTestWorld TestWorld;
	TStrongObjectPtr<UTexture2D> VelocityTexture(CreatePixelTexture(StillVelocityPixels, Resolution));
	TStrongObjectPtr<UTexture2D> AuxTexture(CreatePixelTexture(AuxPixels, Resolution));
	TStrongObjectPtr<UTextureRenderTarget2D> FlowTarget(NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient));
	FlowTarget->RenderTargetFormat = RTF_RGBA16f;
	FlowTarget->ClearColor = FLinearColor(0.5f, 0.5f, 0.0f, 1.0f);
	FlowTarget->bAutoGenerateMips = false;
	FlowTarget->AddressX = TA_Clamp;
	FlowTarget->AddressY = TA_Clamp;
	FlowTarget->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, true);
	FlowTarget->UpdateResourceImmediate(true);
	FlushRenderingCommands();
	if (!TestTrue(TEXT("created inputs and flow target"), VelocityTexture.IsValid() && AuxTexture.IsValid()))
	{
		return false;
	}

	TStrongObjectPtr<UMaterialInstanceDynamic> CopyInstance(UMaterialInstanceDynamic::Create(CopyMaterial, GetTransientPackage()));
	CopyInstance->SetTextureParameterValue(TEXT("VelocityTexture"), VelocityTexture.Get());
	CopyInstance->SetTextureParameterValue(TEXT("AuxTexture"), AuxTexture.Get());
	CopyInstance->SetScalarParameterValue(TEXT("AuxFoliageBendActive"), 1.0f);
	CopyInstance->SetVectorParameterValue(TEXT("PlaneAxisU"), FLinearColor(1.0f, 0.0f, 0.0f, 0.0f));
	CopyInstance->SetVectorParameterValue(TEXT("PlaneAxisV"), FLinearColor(0.0f, 1.0f, 0.0f, 0.0f));
	CopyInstance->SetScalarParameterValue(TEXT("VelocitySpeedReferenceCmPerSec"), OutputSpeedReferenceCmPerSec);
	CopyInstance->SetScalarParameterValue(TEXT("FoliageBendMaxDegrees"), BendMaxDegrees);
	CopyInstance->SetScalarParameterValue(TEXT("FoliageBendSpeedReferenceCmPerSec"), BendSpeedReferenceCmPerSec);
	CopyInstance->SetScalarParameterValue(TEXT("FoliageBendResponse"), BendResponse);
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(TestWorld.World, FlowTarget.Get(), CopyInstance.Get());
	FlushRenderingCommands();

	TArray<FLinearColor> Samples;
	if (!TestTrue(TEXT("read flow target"), UKismetRenderingLibrary::ReadRenderTargetRaw(TestWorld.World, FlowTarget.Get(), Samples, false)) ||
		!TestEqual(TEXT("flow sample count"), Samples.Num(), Resolution * Resolution))
	{
		return false;
	}

	float MaximumError = 0.0f;
	for (int32 PixelY = 0; PixelY < Resolution; ++PixelY)
	{
		for (int32 PixelX = 0; PixelX < Resolution; ++PixelX)
		{
			const FLinearColor Expected = ComputeExpectedFlow(MakePackedVelocity(PixelX, PixelY));
			const FLinearColor& Actual = Samples[PixelY * Resolution + PixelX];
			MaximumError = FMath::Max3(MaximumError, FMath::Abs(Actual.R - Expected.R), FMath::Max(FMath::Abs(Actual.G - Expected.G), FMath::Abs(Actual.B - Expected.B)));
		}
	}
	AddInfo(FString::Printf(TEXT("%s: maximum decoded flow error from aux %.5f"), *CaseLabel, MaximumError));
	TestTrue(TEXT("copy output reads the aux bend and matches the wave 2 decode"), MaximumError <= CopyTolerance);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageBendRhiOffscreenCostTest,
	"TDGame.FluidFXRhi.FoliageBendCost.OffscreenCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageBendRhiOffscreenCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageBendRhiTestsPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.FoliageBendCost.OffscreenCost");
	if (!CanRunFoliageBendRhiTest(*this, CaseLabel))
	{
		return false;
	}

	const FTDFluidSimulationConfig Config = MakeSyntheticConfig();
	const FTDFluidFoliageBendSyntheticInputs CpuInputs = MakeSyntheticInputs(CostCpuResolution);
	const FVector2D CpuCellSizeCm(CostExtentCm / CostCpuResolution, CostExtentCm / CostCpuResolution);
	const FTDFluidFoliageBendParameters CpuParameters = FTDFluidFoliageBendLayer::MakeParameters(Config, StepSeconds, CpuCellSizeCm, true);
	FTDFluidFoliageBendLayer CpuLayer;
	TArray<double> CpuMilliseconds;
	for (int32 FrameIndex = 0; FrameIndex < CostFrameCount; ++FrameIndex)
	{
		const double StartSeconds = FPlatformTime::Seconds();
		CpuLayer.AdvanceCpu(CpuInputs.VelocityPixels, CpuInputs.DensityPixels, CpuInputs.WaterPixels, CostCpuResolution, FVector2D::ZeroVector, CpuCellSizeCm, CpuParameters);
		CpuMilliseconds.Add(1000.0 * (FPlatformTime::Seconds() - StartSeconds));
	}

	const FTDFluidFoliageBendSyntheticInputs GpuInputs = MakeSyntheticInputs(CostGpuResolution);
	const FVector2D GpuCellSizeCm(CostExtentCm / CostGpuResolution, CostExtentCm / CostGpuResolution);
	const FTDFluidFoliageBendParameters GpuParameters = FTDFluidFoliageBendLayer::MakeParameters(Config, StepSeconds, GpuCellSizeCm, true);
	TStrongObjectPtr<UTexture2D> VelocityTexture(CreatePixelTexture(GpuInputs.VelocityPixels, CostGpuResolution));
	TStrongObjectPtr<UTexture2D> DensityTexture(CreatePixelTexture(GpuInputs.DensityPixels, CostGpuResolution));
	TStrongObjectPtr<UTexture2D> WaterTexture(CreatePixelTexture(GpuInputs.WaterPixels, CostGpuResolution));
	TStrongObjectPtr<UTextureRenderTarget2D> AuxTarget(CreateAuxTarget(CostGpuResolution));
	WaitForGpu();
	if (!TestTrue(TEXT("created GPU cost inputs"), VelocityTexture.IsValid() && DensityTexture.IsValid() && WaterTexture.IsValid() && AuxTarget.IsValid()))
	{
		return false;
	}

	FTDFluidFoliageBendLayer GpuLayer;
	TArray<double> GpuMilliseconds;
	for (int32 FrameIndex = 0; FrameIndex < CostFrameCount; ++FrameIndex)
	{
		const double StartSeconds = FPlatformTime::Seconds();
		GpuLayer.SubmitGpu(VelocityTexture.Get(), DensityTexture.Get(), WaterTexture.Get(), AuxTarget.Get(), false, CostGpuResolution, FVector2D::ZeroVector, GpuCellSizeCm, GpuParameters);
		WaitForGpu();
		GpuMilliseconds.Add(1000.0 * (FPlatformTime::Seconds() - StartSeconds));
	}

	const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetNumberField(TEXT("frames"), CostFrameCount);
	Report->SetNumberField(TEXT("cpu_resolution"), CostCpuResolution);
	Report->SetNumberField(TEXT("cpu_median_ms"), ReadPercentile(CpuMilliseconds, 0.5));
	Report->SetNumberField(TEXT("cpu_p95_ms"), ReadPercentile(CpuMilliseconds, 0.95));
	Report->SetNumberField(TEXT("gpu_resolution"), CostGpuResolution);
	Report->SetNumberField(TEXT("gpu_median_ms"), ReadPercentile(GpuMilliseconds, 0.5));
	Report->SetNumberField(TEXT("gpu_p95_ms"), ReadPercentile(GpuMilliseconds, 0.95));
	Report->SetStringField(TEXT("note"), TEXT("record only, CPU = AdvanceCpu wall time, GPU = submit plus block until GPU idle wall time per frame"));
	FString ReportPath;
	TestTrue(TEXT("wrote the foliage bend cost record"), SaveFoliageBendCostRecord(Report, ReportPath));
	AddInfo(FString::Printf(TEXT("TDFluidFoliageBendCost: CPU %d median %.3f ms p95 %.3f ms, GPU %d median %.3f ms p95 %.3f ms -> %s"),
		CostCpuResolution, ReadPercentile(CpuMilliseconds, 0.5), ReadPercentile(CpuMilliseconds, 0.95),
		CostGpuResolution, ReadPercentile(GpuMilliseconds, 0.5), ReadPercentile(GpuMilliseconds, 0.95), *ReportPath));
	return !HasAnyErrors();
}

#endif
