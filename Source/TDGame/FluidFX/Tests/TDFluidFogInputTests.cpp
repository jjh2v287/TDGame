#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidInputBrushRules.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidVolumeDriverComponent.h"
#include "FluidFX/TDFluidVolumeDriverTypes.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "UObject/Package.h"

namespace TDFluidFogInputTestPrivate
{
	constexpr float FogStepSeconds = 1.0f / 60.0f;
	constexpr int32 CutTrailResolution = 128;
	constexpr double CutTrailExtentCm = 4000.0;
	constexpr float CutTrailAmbientDensity = 0.8f;
	constexpr float CutTrailRefillPerSec = 0.3f;
	constexpr float CutTrailEraserDensity = -0.75f;
	constexpr float CutTrailEraserRadiusCm = 100.0f;
	constexpr double CutTrailEraserSpeedCmPerSec = 150.0;
	constexpr double CutTrailEraserStartOffsetCm = 300.0;
	constexpr int32 CutTrailWarmupStepLimit = 1500;
	constexpr int32 FogWrapResolution = 64;
	constexpr double FogWrapExtentCm = 3000.0;
	constexpr int32 FogWrapStepCount = 6;
	constexpr double WrapUvDoubleTolerance = 1.0e-9;
	constexpr double WrapUvFloatTolerance = 2.0e-4;
	constexpr double WrapUvParameterTolerance = 1.0e-4;
	const FVector CpuWrapActorLocation(0.0, 0.0, 0.0);
	const FVector CpuClampActorLocation(100000.0, 0.0, 0.0);
	const FVector GpuWrapActorLocation(200000.0, 0.0, 0.0);
	const FVector TileObserverLocation(211234.0, 7321.0, 500.0);
	const TCHAR* FogTileSyncInstancePath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidFog_TileSync.MI_TD_FluidFog_TileSync");
	const TCHAR* SeaWaterInstancePath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidWater_Sea.MI_TD_FluidWater_Sea");

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	FTDFluidSimulationConfig MakeCutTrailConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = CutTrailResolution;
		Config.SimExtentCm = FVector2D(CutTrailExtentCm, CutTrailExtentCm);
		Config.SolverBackend = ETDFluidSolverBackend::Cpu;
		Config.bEnableWaterLayer = false;
		Config.AmbientDensity = CutTrailAmbientDensity;
		Config.AmbientRefillPerSec = CutTrailRefillPerSec;
		Config.AmbientNoiseAmount = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.VorticityStrength = 0.0f;
		Config.DensityDissipation = 0.0f;
		Config.BorderFadeRatio = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.BrushEraseRatePerSec = 8.0f;
		Config.MaxPointsPerTick = 64;
		return Config;
	}

	void StepCutTrailSolver(FTDFluidGrid2DSolver& Solver, int32 StepCount)
	{
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Solver.StepSimulation(FogStepSeconds);
		}
	}

	FTDFluidSimulationConfig MakeFogWrapConfig(ETDFluidAreaAddressMode AddressMode, ETDFluidSolverBackend Backend)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.AreaAddressMode = AddressMode;
		Config.GridResolution = FogWrapResolution;
		Config.SimExtentCm = FVector2D(FogWrapExtentCm, FogWrapExtentCm);
		Config.SolverBackend = Backend;
		Config.bEnableWaterLayer = false;
		Config.BorderFadeRatio = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.AmbientDensity = 0.5f;
		Config.AmbientRefillPerSec = 0.3f;
		return Config;
	}

	double ComputeWrappedUvDistance(double FirstUv, double SecondUv)
	{
		const double Difference = FirstUv - SecondUv;
		return FMath::Abs(Difference - FMath::RoundToDouble(Difference));
	}

	double ComputeMaximumWrapUvDistance(const FVector2D& PointCm, const FVector2D& CenterCm, const FVector2D& ExtentCm, const FVector2D& OriginCm, bool bUsesFloat)
	{
		if (bUsesFloat)
		{
			const float FogU = (static_cast<float>(PointCm.X) - static_cast<float>(CenterCm.X)) / static_cast<float>(ExtentCm.X) + 0.5f;
			const float FogV = (static_cast<float>(PointCm.Y) - static_cast<float>(CenterCm.Y)) / static_cast<float>(ExtentCm.Y) + 0.5f;
			const float WaterU = (static_cast<float>(PointCm.X) - static_cast<float>(OriginCm.X)) / static_cast<float>(ExtentCm.X);
			const float WaterV = (static_cast<float>(PointCm.Y) - static_cast<float>(OriginCm.Y)) / static_cast<float>(ExtentCm.Y);
			return FMath::Max(ComputeWrappedUvDistance(FogU, WaterU), ComputeWrappedUvDistance(FogV, WaterV));
		}

		const FVector2D FogUv = (PointCm - CenterCm) / ExtentCm + FVector2D(0.5, 0.5);
		const FVector2D WaterUv = (PointCm - OriginCm) / ExtentCm;
		return FMath::Max(ComputeWrappedUvDistance(FogUv.X, WaterUv.X), ComputeWrappedUvDistance(FogUv.Y, WaterUv.Y));
	}

	struct FTDFluidFogTestWorld
	{
		FTDFluidFogTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidFogInputWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidFogTestWorld()
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

		void StepWorld(bool bShouldWaitForGpu)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, FogStepSeconds);
			if (bShouldWaitForGpu)
			{
				ENQUEUE_RENDER_COMMAND(TDFluidFogInputWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
				{
					RHICmdList.SubmitAndBlockUntilGPUIdle();
				});
			}
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	ATDFluidLiveActor* SpawnFogLiveActor(UWorld& TargetWorld, const FVector& Location, const FTDFluidSimulationConfig& Config)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
		ATDFluidLiveActor* LiveActor = TargetWorld.SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!LiveActor || !LiveActor->LiveComponent)
		{
			return nullptr;
		}

		LiveActor->LiveComponent->SimulationConfig = Config;
		LiveActor->FinishSpawning(SpawnTransform);
		return LiveActor;
	}

	bool ReadScalarParameter(const UMaterialInstanceDynamic& TargetMID, const TCHAR* ParameterName, float& OutValue)
	{
		return TargetMID.GetScalarParameterValue(FHashedMaterialParameterInfo(ParameterName), OutValue);
	}

	bool ReadVectorParameter(const UMaterialInstanceDynamic& TargetMID, const TCHAR* ParameterName, FLinearColor& OutValue)
	{
		return TargetMID.GetVectorParameterValue(FHashedMaterialParameterInfo(ParameterName), OutValue);
	}

	UTexture* ReadTextureParameter(const UMaterialInstanceDynamic& TargetMID, const TCHAR* ParameterName)
	{
		UTexture* Texture = nullptr;
		return TargetMID.GetTextureParameterValue(FHashedMaterialParameterInfo(ParameterName), Texture) ? Texture : nullptr;
	}

	bool IsWrapAddressed(const UTexture* Texture)
	{
		if (const UTextureRenderTarget2D* RenderTarget = Cast<UTextureRenderTarget2D>(Texture))
		{
			return RenderTarget->AddressX == TA_Wrap && RenderTarget->AddressY == TA_Wrap;
		}
		if (const UTexture2D* CpuTexture = Cast<UTexture2D>(Texture))
		{
			return CpuTexture->AddressX == TA_Wrap && CpuTexture->AddressY == TA_Wrap;
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFogInputCutTrailRefillsTest,
	"TDGame.FluidFX.FogInput.CutTrailRefills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFogInputCutTrailRefillsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFogInputTestPrivate;

	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(MakeCutTrailConfig(), FVector2D::ZeroVector);
	if (!TestTrue(TEXT("The cut trail solver initializes"), Solver.IsInitialized()))
	{
		return false;
	}

	const int32 ProbeCell = Solver.GetResolution() / 2;
	const double CellSizeCm = CutTrailExtentCm / Solver.GetResolution();
	const FVector2D ProbeCm(-0.5 * CutTrailExtentCm + (ProbeCell + 0.5) * CellSizeCm, -0.5 * CutTrailExtentCm + (ProbeCell + 0.5) * CellSizeCm);
	const auto SampleProbe = [&Solver, ProbeCell]() { return static_cast<double>(Solver.SampleAtCell(ProbeCell, ProbeCell).PrimaryDensity); };

	int32 WarmupSteps = 0;
	while (WarmupSteps < CutTrailWarmupStepLimit && SampleProbe() < 0.99 * CutTrailAmbientDensity)
	{
		StepCutTrailSolver(Solver, 1);
		++WarmupSteps;
	}
	const double DensityBeforePass = SampleProbe();
	if (!TestTrue(TEXT("Warm up fills the probe to 99 percent of ambient"), DensityBeforePass >= 0.99 * CutTrailAmbientDensity))
	{
		return false;
	}

	FTDFluidBrushStamp Eraser;
	Eraser.RadiusCm = CutTrailEraserRadiusCm;
	Eraser.DensityStrength = CutTrailEraserDensity;
	Eraser.VelocityStrength = 0.0f;
	Eraser.WorldVelocity = FVector(CutTrailEraserSpeedCmPerSec, 0.0, 0.0);
	double EraserX = ProbeCm.X - CutTrailEraserStartOffsetCm;
	while (EraserX <= ProbeCm.X + CutTrailEraserRadiusCm + CellSizeCm)
	{
		Eraser.WorldLocation = FVector(EraserX, ProbeCm.Y, 0.0);
		Solver.ApplyBrushStamps({ Eraser }, FogStepSeconds);
		Solver.StepSimulation(FogStepSeconds);
		EraserX += CutTrailEraserSpeedCmPerSec * FogStepSeconds;
	}
	const double DensityAfterPass = SampleProbe();

	const double RecoverySeconds = FMath::Loge(5.0) / CutTrailRefillPerSec;
	const int32 RecoverySteps = FMath::RoundToInt32(RecoverySeconds / FogStepSeconds);
	const int32 HalfRecoverySteps = RecoverySteps / 2;
	StepCutTrailSolver(Solver, HalfRecoverySteps);
	const double DensityAtHalfRecovery = SampleProbe();
	StepCutTrailSolver(Solver, RecoverySteps - HalfRecoverySteps);
	const double DensityAtRecovery = SampleProbe();

	AddInfo(FString::Printf(TEXT("TDFluidFogCutTrail: warmup_steps=%d before=%.6f after=%.6f half=%.6f recovery=%.6f recovery_steps=%d ambient=%.3f"),
		WarmupSteps, DensityBeforePass, DensityAfterPass, DensityAtHalfRecovery, DensityAtRecovery, RecoverySteps, CutTrailAmbientDensity));
	TestTrue(TEXT("The eraser pass cuts the trail center below 30 percent of the density before the pass"), DensityAfterPass <= 0.3 * DensityBeforePass);
	TestTrue(TEXT("Half the recovery time stays below 75 percent of ambient"), DensityAtHalfRecovery < 0.75 * CutTrailAmbientDensity);
	TestTrue(TEXT("The trail refills to 78 to 90 percent of ambient after ln5 over the refill rate"), DensityAtRecovery >= 0.78 * CutTrailAmbientDensity && DensityAtRecovery <= 0.90 * CutTrailAmbientDensity);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFogInputKindsDataContractTest,
	"TDGame.FluidFX.FogInput.FogKindsDataContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFogInputKindsDataContractTest::RunTest(const FString& Parameters)
{
	const FTDFluidSimulationConfig MistConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::FogMist);
	TestTrue(TEXT("FogMist actors erase fog"), MistConfig.ActorBrushDensityStrength < 0.0f);
	TestTrue(TEXT("FogMist refills toward ambient"), MistConfig.AmbientRefillPerSec > 0.0f && MistConfig.AmbientDensity > 0.0f);
	TestTrue(TEXT("FogMist keeps the clamp address mode"), MistConfig.AreaAddressMode == ETDFluidAreaAddressMode::Clamp);

	const FTDFluidSimulationConfig StirConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::FogMistStir);
	const FTDFluidInputBrushSettings StirBrush = UTDFluidPresetAsset::BuildInputBrushForPresetKind(ETDFluidPresetKind::FogMistStir);
	TestTrue(TEXT("FogMistStir actors leave density alone"), StirConfig.ActorBrushDensityStrength == 0.0f);
	TestTrue(TEXT("FogMistStir selects every source as velocity only"), StirBrush.VelocityOnlySources == ETDFluidVelocityOnlySources::All);
	TestTrue(TEXT("FogMistStir turns off mass point density"), StirBrush.MassPointDensityStrength == 0.0f);

	const FTDFluidSimulationConfig TileConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::FogTileSim);
	TestTrue(TEXT("FogTileSim wraps its area"), TileConfig.AreaAddressMode == ETDFluidAreaAddressMode::Wrap);
	TestTrue(TEXT("FogTileSim keeps no border fade"), TileConfig.BorderFadeRatio == 0.0f);
	TestTrue(TEXT("FogTileSim enlarges its fog box"), UTDFluidPresetAsset::BuildVolumeRenderForPresetKind(ETDFluidPresetKind::FogTileSim).VolumeSizeMultiplier > 1.0f);

	const ETDFluidPresetKind FogKinds[] = { ETDFluidPresetKind::FogMist, ETDFluidPresetKind::FogMistStir, ETDFluidPresetKind::FogTileSim };
	for (const ETDFluidPresetKind Kind : FogKinds)
	{
		TestFalse(*(UEnum::GetValueAsString(Kind) + TEXT(" is not a fire family kind")), UTDFluidPresetAsset::IsFireFamilyPresetKind(Kind));
	}

	constexpr double MaximumCaptureTexelCm = 32.0;
	for (const ETDFluidPresetKind Kind : FogKinds)
	{
		const FString KindName = UEnum::GetValueAsString(Kind);
		const FTDFluidVolumeDriverSettings FogDriver = UTDFluidPresetAsset::BuildVolumeDriverForPresetKind(Kind);
		const FTDFluidTerrainHeightSettings FogTerrain = UTDFluidPresetAsset::BuildTerrainHeightForPresetKind(Kind);
		TestTrue(*(KindName + TEXT(" lets the fog floor follow the terrain")), FogDriver.bFogFloorFollowsTerrain);
		if (!FogDriver.bFogFloorFollowsTerrain)
		{
			TestTrue(*(KindName + TEXT(" reads no floor height while it does not follow the terrain")), FogTerrain.VolumeHeightSource == ETDFluidVolumeHeightSource::None);
			continue;
		}
		TestTrue(*(KindName + TEXT(" reads the floor height from the scene capture")), FogTerrain.VolumeHeightSource == ETDFluidVolumeHeightSource::SceneCapture);
		TestTrue(*(KindName + TEXT(" captures terrain only")), FogTerrain.SceneCaptureContent == ETDFluidHeightCaptureContent::TerrainOnly);
		TestEqual(*(KindName + TEXT(" captures from 2000 cm above its owner")), FogTerrain.SceneCaptureHeightAboveCm, 2000.0f);

		const FTDFluidSimulationConfig FogConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(Kind);
		const FTDFluidVolumeRenderSettings FogRender = UTDFluidPresetAsset::BuildVolumeRenderForPresetKind(Kind);
		const bool bIsWrapped = FogConfig.AreaAddressMode == ETDFluidAreaAddressMode::Wrap;
		const double MaximumExtentCm = FMath::Max(FogConfig.SimExtentCm.X, FogConfig.SimExtentCm.Y);
		const double CoverMultiplier = UTDFluidVolumeDriverComponent::GetHeightCaptureCoverMultiplier(true, bIsWrapped, FogRender.VolumeSizeMultiplier);
		const double CaptureWidthCm = MaximumExtentCm * CoverMultiplier * (1.0 + 2.0 * FogTerrain.SceneCaptureMarginRatio);
		const double CaptureTexelCm = CaptureWidthCm / FMath::Max(FogTerrain.SceneCaptureResolution, 1);
		TestTrue(*FString::Printf(TEXT("%s capture texel %.2f cm stays within %.0f cm"), *KindName, CaptureTexelCm, MaximumCaptureTexelCm), CaptureTexelCm <= MaximumCaptureTexelCm);
		if (bIsWrapped)
		{
			TestTrue(*(KindName + TEXT(" keeps its wrapped fog box centred on the capture")), FMath::IsNearlyZero(FogRender.VolumePositionOffsetCm.X) && FMath::IsNearlyZero(FogRender.VolumePositionOffsetCm.Y));
			continue;
		}
		const double RecaptureDistanceCm = FogTerrain.SceneCaptureRecaptureCells * FogConfig.SimExtentCm.X / FMath::Max(FogConfig.GridResolution, 1);
		const double CaptureMarginCm = FogTerrain.SceneCaptureMarginRatio * MaximumExtentCm;
		TestTrue(*FString::Printf(TEXT("%s recaptures after %.1f cm, within the %.1f cm capture margin"), *KindName, RecaptureDistanceCm, CaptureMarginCm), RecaptureDistanceCm <= CaptureMarginCm);
	}

	const FTDFluidVolumeDriverSettings SmokeDriver = UTDFluidPresetAsset::BuildVolumeDriverForPresetKind(ETDFluidPresetKind::Smoke);
	const FTDFluidTerrainHeightSettings SmokeTerrain = UTDFluidPresetAsset::BuildTerrainHeightForPresetKind(ETDFluidPresetKind::Smoke);
	TestFalse(TEXT("Smoke keeps its placed floor instead of following the terrain"), SmokeDriver.bFogFloorFollowsTerrain);
	TestTrue(TEXT("Smoke reads no floor height and so builds no driver"), SmokeTerrain.VolumeHeightSource == ETDFluidVolumeHeightSource::None);

	const ETDFluidPresetKind UnchangedKinds[] = { ETDFluidPresetKind::Smoke, ETDFluidPresetKind::Water, ETDFluidPresetKind::Torch, ETDFluidPresetKind::TileWaves, ETDFluidPresetKind::FogMist, ETDFluidPresetKind::FogTileSim };
	for (const ETDFluidPresetKind Kind : UnchangedKinds)
	{
		TestFalse(*(UEnum::GetValueAsString(Kind) + TEXT(" keeps every input brush rule off")), FTDFluidInputBrushRules::HasActiveRules(UTDFluidPresetAsset::BuildInputBrushForPresetKind(Kind)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFogTileSelfWrapUvTest,
	"TDGame.FluidFX.FogTile.SelfWrapUvMatchesTileMapOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFogTileSelfWrapUvTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFogInputTestPrivate;

	const FVector2D Centers[] = { FVector2D(0.0, 0.0), FVector2D(17000.0, -35000.0), FVector2D(-123456.5, 98765.25), FVector2D(200000.0, -150000.0) };
	const FVector2D Extents[] = { FVector2D(3000.0, 3000.0), FVector2D(4000.0, 2500.0) };
	const FVector2D Observers[] = { FVector2D(0.0, 0.0), FVector2D(-1.0e6, 1.0e6), FVector2D(54321.0, -12345.0) };
	const FVector2D Points[] = {
		FVector2D(0.0, 0.0), FVector2D(-1.0, -1.0), FVector2D(1499.999, -1500.001), FVector2D(-1500.0, 1500.0), FVector2D(-3000.0, 3000.0),
		FVector2D(-98765.4321, 12345.678), FVector2D(98765.4321, -12345.678), FVector2D(1.0e6, -1.0e6), FVector2D(-1.0e6, 1.0e6), FVector2D(250000.0, 0.5),
		FVector2D(-0.000001, 2999.999999), FVector2D(17000.0, -35000.0), FVector2D(15500.0, -36500.0), FVector2D(-123456.5, 98765.25), FVector2D(199999.9, -149999.9),
		FVector2D(12.5, -7.25), FVector2D(-45678.9, -45678.9), FVector2D(333333.3, 666666.6), FVector2D(-2.0e5, 2.0e5), FVector2D(1.5e5, -1.75e5)
	};

	double MaximumDoubleDistance = 0.0;
	double MaximumFloatDistance = 0.0;
	for (const FVector2D& CenterCm : Centers)
	{
		for (const FVector2D& ExtentCm : Extents)
		{
			const FVector2D AnchorCm = CenterCm - 0.5 * ExtentCm;
			for (const FVector2D& ObserverCm : Observers)
			{
				const FVector2D OriginCm = UTDFluidLiveComponent::ComputeTileMapOriginCm(AnchorCm, ExtentCm, ObserverCm);
				for (const FVector2D& PointCm : Points)
				{
					MaximumDoubleDistance = FMath::Max(MaximumDoubleDistance, ComputeMaximumWrapUvDistance(PointCm, CenterCm, ExtentCm, OriginCm, false));
					const bool bFitsFloatRange = FMath::Abs(PointCm.X) <= 2.0e5 && FMath::Abs(PointCm.Y) <= 2.0e5 && FMath::Abs(CenterCm.X) <= 2.0e5 && FMath::Abs(CenterCm.Y) <= 2.0e5
						&& FMath::Abs(OriginCm.X) <= 2.0e5 && FMath::Abs(OriginCm.Y) <= 2.0e5;
					if (bFitsFloatRange)
					{
						MaximumFloatDistance = FMath::Max(MaximumFloatDistance, ComputeMaximumWrapUvDistance(PointCm, CenterCm, ExtentCm, OriginCm, true));
					}
				}
			}
		}
	}

	AddInfo(FString::Printf(TEXT("TDFluidFogTileUv: max_double_distance=%.3e max_float_distance=%.3e"), MaximumDoubleDistance, MaximumFloatDistance));
	TestTrue(TEXT("Fog self UV and water tile UV share the fractional texel in double"), MaximumDoubleDistance <= WrapUvDoubleTolerance);
	TestTrue(TEXT("Fog self UV and water tile UV share the fractional texel in float within 2e5 cm"), MaximumFloatDistance <= WrapUvFloatTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFogTileCpuWrapVolumeParametersTest,
	"TDGame.FluidFXRhi.FogTile.CpuWrapVolumeParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidFogTileCpuWrapVolumeParametersTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFogInputTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("TDGame.FluidFXRhi.FogTile.CpuWrapVolumeParameters needs a rendering RHI."));
		return false;
	}

	UMaterialInterface* FogTileInstance = LoadObject<UMaterialInterface>(nullptr, FogTileSyncInstancePath);
	if (!TestNotNull(TEXT("Loaded the fog tile sync instance (made by the fog material step)"), FogTileInstance))
	{
		return false;
	}

	FTDFluidFogTestWorld TestWorld;
	ATDFluidLiveActor* WrapActor = SpawnFogLiveActor(*TestWorld.World, CpuWrapActorLocation, MakeFogWrapConfig(ETDFluidAreaAddressMode::Wrap, ETDFluidSolverBackend::Cpu));
	ATDFluidLiveActor* ClampActor = SpawnFogLiveActor(*TestWorld.World, CpuClampActorLocation, MakeFogWrapConfig(ETDFluidAreaAddressMode::Clamp, ETDFluidSolverBackend::Cpu));
	UMaterialInstanceDynamic* WrapFogMID = UMaterialInstanceDynamic::Create(FogTileInstance, GetTransientPackage());
	UMaterialInstanceDynamic* ClampFogMID = UMaterialInstanceDynamic::Create(FogTileInstance, GetTransientPackage());
	if (!TestNotNull(TEXT("Spawned the CPU wrap actor"), WrapActor) || !TestNotNull(TEXT("Spawned the CPU clamp actor"), ClampActor)
		|| !TestNotNull(TEXT("Created the wrap fog MID"), WrapFogMID) || !TestNotNull(TEXT("Created the clamp fog MID"), ClampFogMID))
	{
		return false;
	}

	WrapActor->LiveComponent->RegisterExternalMaterialTarget(WrapFogMID);
	ClampActor->LiveComponent->RegisterExternalMaterialTarget(ClampFogMID);
	for (int32 StepIndex = 0; StepIndex < FogWrapStepCount; ++StepIndex)
	{
		TestWorld.StepWorld(false);
	}

	const UTDFluidLiveComponent& WrapComponent = *WrapActor->LiveComponent;
	if (!TestFalse(TEXT("The wrap actor runs on the CPU"), WrapComponent.IsUsingGpuSolver()) || !TestTrue(TEXT("The wrap actor is wrapped"), WrapComponent.IsAreaWrapped()))
	{
		return false;
	}

	float WrapFlag = -1.0f;
	float ClampFlag = -1.0f;
	TestTrue(TEXT("The wrap fog MID reads SimAreaWrapped"), ReadScalarParameter(*WrapFogMID, TEXT("SimAreaWrapped"), WrapFlag));
	TestTrue(TEXT("The clamp fog MID reads SimAreaWrapped"), ReadScalarParameter(*ClampFogMID, TEXT("SimAreaWrapped"), ClampFlag));
	TestEqual(TEXT("The wrap source marks its fog volume as wrapped"), WrapFlag, 1.0f);
	TestEqual(TEXT("The clamp source keeps its fog volume unwrapped"), ClampFlag, 0.0f);

	UTexture* WrapSourceTexture = WrapComponent.GetDensityPressureTexture();
	UTexture* WrapFogTexture = ReadTextureParameter(*WrapFogMID, TEXT("DensityPressureTexture"));
	TestTrue(TEXT("The wrap source publishes a density pressure texture"), WrapSourceTexture != nullptr);
	TestTrue(TEXT("The wrap fog volume samples the wrap source output"), WrapFogTexture != nullptr && WrapFogTexture == WrapSourceTexture);
	TestTrue(TEXT("The wrap source output uses the wrap address mode"), IsWrapAddressed(WrapSourceTexture));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFogTileGpuWrapVolumeParametersTest,
	"TDGame.FluidFXRhi.FogTile.GpuWrapVolumeParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidFogTileGpuWrapVolumeParametersTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFogInputTestPrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("TDGame.FluidFXRhi.FogTile.GpuWrapVolumeParameters needs a rendering RHI."));
		return false;
	}

	UMaterialInterface* FogTileInstance = LoadObject<UMaterialInterface>(nullptr, FogTileSyncInstancePath);
	UMaterialInterface* SeaWaterInstance = LoadObject<UMaterialInterface>(nullptr, SeaWaterInstancePath);
	if (!TestNotNull(TEXT("Loaded the fog tile sync instance (made by the fog material step)"), FogTileInstance) || !TestNotNull(TEXT("Loaded the sea water instance"), SeaWaterInstance))
	{
		return false;
	}

	FTDFluidFogTestWorld TestWorld;
	ATDFluidLiveActor* WrapActor = SpawnFogLiveActor(*TestWorld.World, GpuWrapActorLocation, MakeFogWrapConfig(ETDFluidAreaAddressMode::Wrap, ETDFluidSolverBackend::Gpu));
	APlayerController* ObserverController = TestWorld.World->SpawnActor<APlayerController>();
	UMaterialInstanceDynamic* FogMID = UMaterialInstanceDynamic::Create(FogTileInstance, GetTransientPackage());
	UMaterialInstanceDynamic* WaterMID = UMaterialInstanceDynamic::Create(SeaWaterInstance, GetTransientPackage());
	if (!TestNotNull(TEXT("Spawned the GPU wrap actor"), WrapActor) || !TestNotNull(TEXT("Spawned the observer controller"), ObserverController)
		|| !TestNotNull(TEXT("Created the fog MID"), FogMID) || !TestNotNull(TEXT("Created the water MID"), WaterMID))
	{
		return false;
	}

	APlayerCameraManager* ObserverCamera = ObserverController->PlayerCameraManager;
	if (!TestNotNull(TEXT("The observer controller owns a camera manager"), ObserverCamera))
	{
		return false;
	}

	WrapActor->LiveComponent->RegisterExternalMaterialTarget(FogMID);
	WrapActor->LiveComponent->RegisterTileMapTarget(WaterMID);
	FMinimalViewInfo ObserverView;
	ObserverView.Location = TileObserverLocation;
	for (int32 StepIndex = 0; StepIndex < FogWrapStepCount; ++StepIndex)
	{
		ObserverCamera->SetCameraCachePOV(ObserverView);
		TestWorld.StepWorld(true);
	}

	const UTDFluidLiveComponent& WrapComponent = *WrapActor->LiveComponent;
	if (!TestTrue(TEXT("The wrap actor runs on the GPU"), WrapComponent.IsUsingGpuSolver()) || !TestTrue(TEXT("The wrap actor is wrapped"), WrapComponent.IsAreaWrapped()))
	{
		return false;
	}

	float WrapFlag = -1.0f;
	FLinearColor FogCenter;
	FLinearColor FogExtent;
	FLinearColor WaterOrigin;
	FLinearColor WaterPeriod;
	if (!TestTrue(TEXT("The fog MID reads SimAreaWrapped"), ReadScalarParameter(*FogMID, TEXT("SimAreaWrapped"), WrapFlag))
		|| !TestTrue(TEXT("The fog MID reads SimCenterWorld"), ReadVectorParameter(*FogMID, TEXT("SimCenterWorld"), FogCenter))
		|| !TestTrue(TEXT("The fog MID reads SimExtentsCm"), ReadVectorParameter(*FogMID, TEXT("SimExtentsCm"), FogExtent))
		|| !TestTrue(TEXT("The water MID reads TileMap1OriginCm"), ReadVectorParameter(*WaterMID, TEXT("TileMap1OriginCm"), WaterOrigin))
		|| !TestTrue(TEXT("The water MID reads TileMap1PeriodCm"), ReadVectorParameter(*WaterMID, TEXT("TileMap1PeriodCm"), WaterPeriod)))
	{
		return false;
	}

	UTexture* FogTexture = ReadTextureParameter(*FogMID, TEXT("DensityPressureTexture"));
	UTexture* WaterTileTexture = ReadTextureParameter(*WaterMID, TEXT("TileMap1Texture"));
	TestEqual(TEXT("The GPU wrap source marks its fog volume as wrapped"), WrapFlag, 1.0f);
	TestTrue(TEXT("The fog volume and the water tile map sample the same source object"), FogTexture != nullptr && FogTexture == WaterTileTexture);
	TestTrue(TEXT("The shared source uses the wrap address mode"), IsWrapAddressed(FogTexture));

	const FVector2D CenterCm(FogCenter.R, FogCenter.G);
	const FVector2D ExtentCm(FogExtent.R, FogExtent.G);
	const FVector2D OriginCm(WaterOrigin.R, WaterOrigin.G);
	const FVector2D PeriodCm(WaterPeriod.R, WaterPeriod.G);
	TestTrue(TEXT("The water tile period equals the fog extent"), PeriodCm.Equals(ExtentCm, WrapUvParameterTolerance));
	double MaximumDistance = 0.0;
	for (int32 PointIndex = 0; PointIndex < 10; ++PointIndex)
	{
		const FVector2D PointCm(TileObserverLocation.X + 731.0 * PointIndex - 3000.0, TileObserverLocation.Y - 517.0 * PointIndex + 1200.0);
		MaximumDistance = FMath::Max(MaximumDistance, ComputeMaximumWrapUvDistance(PointCm, CenterCm, ExtentCm, OriginCm, false));
	}
	AddInfo(FString::Printf(TEXT("TDFluidFogTileGpu: center=(%.3f, %.3f) extent=(%.3f, %.3f) origin=(%.3f, %.3f) max_uv_distance=%.3e"),
		CenterCm.X, CenterCm.Y, ExtentCm.X, ExtentCm.Y, OriginCm.X, OriginCm.Y, MaximumDistance));
	TestTrue(TEXT("Fog self UV and water tile UV agree on ten points"), MaximumDistance <= WrapUvParameterTolerance);
	return true;
}

#endif
