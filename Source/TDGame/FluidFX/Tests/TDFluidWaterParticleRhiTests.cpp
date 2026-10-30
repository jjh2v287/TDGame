#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidMassInputTargets.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterActor.h"
#include "FluidFX/TDFluidWaterParticleActor.h"
#include "FluidFX/TDFluidWaterParticleTypes.h"
#include "FluidFX/TDFluidWaterSplashRules.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraDataChannelAsset.h"
#include "NiagaraGPUProfilerInterface.h"
#include "NiagaraSimCache.h"
#include "NiagaraSystem.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Tickable.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include <limits>

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidWaterParticleRhiTests, Log, All);

namespace TDFluidWaterParticleRhiTestPrivate
{
	constexpr float StepSeconds = 1.0f / 60.0f;
	constexpr int32 ContactResolution = 64;
	constexpr double ContactCellSizeCm = 25.0;
	constexpr int32 ContactPointCount = 600;
	constexpr int32 ContactWarmupSteps = 8;
	constexpr int32 ContactStampSteps = 6;
	constexpr int32 ContactSettleSteps = 4;
	constexpr int32 ProbeHalfSpanCells = 2;
	constexpr float WaterSurfaceZCm = 0.0f;
	constexpr float FloorTopZCm = -100.0f;
	constexpr float CubeHalfSizeCm = 50.0f;
	constexpr int32 SceneResolution = 128;
	constexpr double SceneCellSizeCm = 25.0;
	constexpr double SceneRegionHalfExtentCm = 2400.0;
	constexpr int32 SceneReadyStepLimit = 900;
	constexpr int32 ComponentReadyStepLimit = 240;
	constexpr int32 ProbeArrivalStepLimit = 60;
	constexpr int32 DebrisFollowSteps = 240;
	constexpr int32 DebrisMinimumCount = 240;
	constexpr int32 DebrisProbeCount = 64;
	constexpr float WaveAmplitudeCm = 10.0f;
	constexpr int32 FlowSteps = 180;
	constexpr float FlowSpeedCmPerSec = 200.0f;
	constexpr int32 SplashEarlySteps = 6;
	constexpr int32 SplashContactSteps = 120;
	constexpr int32 SplashTailSteps = 60;
	constexpr float SplashImpactSpeedCmPerSec = 600.0f;
	constexpr float SplashImpactRadiusCm = 15.0f;
	constexpr double SplashWhitewaterReadRadiusCm = 200.0;
	constexpr int32 WhitewaterSteps = 120;
	constexpr double WhitewaterStampMinX = -1500.0;
	constexpr double WhitewaterStampMaxX = -300.0;
	constexpr int32 OutputOnlySteps = 120;
	constexpr int32 CostFrameCount = 120;
	constexpr int32 CostSplashCount = 12;
	const TCHAR* const WaterGpuVariableName = TEXT("TD.FluidFX.WaterGpu");
	const TCHAR* const SolverBackendVariableName = TEXT("TD.FluidFX.SolverBackend");
	const TCHAR* const DebrisSystemPath = TEXT("/Game/FluidFX/Niagara/NS_TD_FluidWaterDebris.NS_TD_FluidWaterDebris");
	const TCHAR* const WhitewaterSystemPath = TEXT("/Game/FluidFX/Niagara/NS_TD_FluidWaterWhitewater.NS_TD_FluidWaterWhitewater");
	const TCHAR* const SplashSystemPath = TEXT("/Game/FluidFX/Niagara/NS_TD_FluidWaterSplash.NS_TD_FluidWaterSplash");
	const TCHAR* const ContactChannelPath = TEXT("/Game/FluidFX/Niagara/DataChannels/NDC_TD_FluidWaterContact.NDC_TD_FluidWaterContact");
	const TCHAR* const CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const FName DebrisEmitterName(TEXT("Debris"));
	const FName WhitewaterEmitterName(TEXT("Whitewater"));
	const FName SplashEmitterName(TEXT("Splash"));
	const FName PositionAttributeName(TEXT("Position"));
	const FName VelocityAttributeName(TEXT("Velocity"));

	struct FTDWaterParticleConsoleVariableScope
	{
		FTDWaterParticleConsoleVariableScope(const TCHAR* Name, int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (!Variable)
			{
				return;
			}

			PreviousValue = Variable->GetString();
			Variable->Set(Value, ECVF_SetByCode);
		}

		~FTDWaterParticleConsoleVariableScope()
		{
			if (Variable)
			{
				Variable->Set(*PreviousValue, ECVF_SetByCode);
			}
		}

		FTDWaterParticleConsoleVariableScope(const FTDWaterParticleConsoleVariableScope&) = delete;
		FTDWaterParticleConsoleVariableScope& operator=(const FTDWaterParticleConsoleVariableScope&) = delete;

		bool IsValid() const
		{
			return Variable != nullptr;
		}

		IConsoleVariable* Variable = nullptr;
		FString PreviousValue;
	};

	struct FTDWaterGpuSelectionScope
	{
		FTDWaterGpuSelectionScope()
			: SolverBackendScope(SolverBackendVariableName, 0)
			, WaterGpuScope(WaterGpuVariableName, 1)
		{
		}

		FTDWaterParticleConsoleVariableScope SolverBackendScope;
		FTDWaterParticleConsoleVariableScope WaterGpuScope;
	};

	bool CanRunWaterParticleRhiTest(FAutomationTestBase& Test, const FString& CaseLabel, const FTDWaterGpuSelectionScope& GpuScope)
	{
		const bool bHasRenderingRhi = !GUsingNullRHI && FApp::CanEverRender();
		UE_LOG(LogTDFluidWaterParticleRhiTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), *CaseLabel, bHasRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (!bHasRenderingRhi)
		{
			Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), *CaseLabel));
			return false;
		}
		if (!GpuScope.WaterGpuScope.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("%s: TD.FluidFX.WaterGpu is not registered"), *CaseLabel));
			return false;
		}
		return true;
	}

	void ReportLine(FAutomationTestBase& Test, const FString& Line)
	{
		UE_LOG(LogTDFluidWaterParticleRhiTests, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
	}

	struct FTDWaterParticleRhiWorld
	{
		FTDWaterParticleRhiWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidWaterParticleRhiWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDWaterParticleRhiWorld()
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

		FTDWaterParticleRhiWorld(const FTDWaterParticleRhiWorld&) = delete;
		FTDWaterParticleRhiWorld& operator=(const FTDWaterParticleRhiWorld&) = delete;

		void StepWorld()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, StepSeconds);
			ENQUEUE_RENDER_COMMAND(TDFluidWaterParticleRhiWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

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

	FTDFluidSimulationConfig MakeContactGpuConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.SolverBackend = ETDFluidSolverBackend::Gpu;
		Config.GridResolution = ContactResolution;
		Config.SimExtentCm = FVector2D(ContactResolution * ContactCellSizeCm, ContactResolution * ContactCellSizeCm);
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = 0.35f;
		Config.PressureIterations = 12;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 1.0f;
		Config.MaxPointsPerTick = 64;
		Config.bEnableWaterLayer = true;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.AmbientDensity = 0.0f;
		Config.RapidsWhitewaterPerSec = 0.0f;
		Config.WhitewaterFromVorticity = 0.0f;
		Config.RippleWaveSpeedCmPerSec = 0.0f;
		Config.SwashHeightCm = 0.0f;
		Config.BankWetHeightCm = 0.0f;
		Config.ActorBrushRippleImpulseCm = 0.0f;
		return Config;
	}

	TArray<FTDFluidMassInputPoint> MakeContactPoints(const FVector& CenterWorld)
	{
		TArray<FTDFluidMassInputPoint> Points;
		Points.Reserve(ContactPointCount);
		for (int32 PointIndex = 0; PointIndex < ContactPointCount; ++PointIndex)
		{
			const int32 Column = PointIndex % 25;
			const int32 Row = PointIndex / 25;
			FTDFluidMassInputPoint& Point = Points.AddDefaulted_GetRef();
			Point.WorldLocation = CenterWorld + FVector((Column - 12) * 6.0, (Row - 12) * 6.0, 0.0);
			Point.WorldVelocity = FVector(30.0 + Column, -20.0 + Row, -50.0);
			Point.RadiusCm = PointIndex % 5 == 0 ? 0.0f : 8.0f + (PointIndex % 7);
			Point.DensityStrength = 0.2f + 0.1f * (PointIndex % 9);
			Point.HeatStrength = 0.4f;
			Point.RippleImpulseCm = 3.0f;
			if (PointIndex % 4 == 0)
			{
				Point.DensityStrength = PointIndex % 8 == 0 ? 0.0f : -0.5f;
			}
		}
		return Points;
	}

	TArray<FTDFluidBrushStamp> MakeContactStamps(const FTDFluidSimulationConfig& Config)
	{
		const FTDFluidWaterParticleSettings Settings = FTDFluidWaterParticleSettings();
		const int32 StampBudget = FTDFluidWaterSplashRules::ResolveContactStampBudget(Settings, Config.MaxPointsPerTick, 0);
		TArray<FTDFluidMassInputPoint> Points = MakeContactPoints(FVector::ZeroVector);
		TArray<FTDFluidBrushStamp> Stamps;
		FTDFluidWaterSplashRules::BuildContactWhitewaterStamps(Points, FVector::ZeroVector, StampBudget, Settings.ContactReadFallbackRadiusCm, Stamps);
		return Stamps;
	}

	struct FTDContactArmResult
	{
		int32 Resolution = 0;
		TArray<FFloat16Color> DensityPixels;
	};

	bool RunContactArm(FAutomationTestBase& Test, const FString& ArmLabel, const TArray<FTDFluidBrushStamp>& Stamps, FTDContactArmResult& OutResult)
	{
		FTDWaterParticleRhiWorld ArmWorld;
		const FTDFluidSimulationConfig Config = MakeContactGpuConfig();
		const FTransform SpawnTransform(FRotator::ZeroRotator, FVector::ZeroVector);
		ATDFluidLiveActor* LiveActor = ArmWorld.World->SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!Test.TestTrue(*(ArmLabel + TEXT(" spawned the GPU water live actor")), LiveActor && LiveActor->LiveComponent))
		{
			return false;
		}
		LiveActor->LiveComponent->SimulationConfig = Config;
		LiveActor->FinishSpawning(SpawnTransform);

		UTDFluidLiveComponent& Component = *LiveActor->LiveComponent;
		const double RegionHalfExtentCm = 0.5 * Config.SimExtentCm.X + 2.0 * ContactCellSizeCm;
		FTDFluidWaterRegion Region;
		Region.MinXY = FVector2D(-RegionHalfExtentCm, -RegionHalfExtentCm);
		Region.MaxXY = FVector2D(RegionHalfExtentCm, RegionHalfExtentCm);
		Region.SurfaceHeightCm = WaterSurfaceZCm;
		TArray<FTDFluidWaterRegion> Regions;
		Regions.Add(Region);
		Component.SetWaterGuidesAndRegions(TArray<FTDFluidFlowGuide>(), Regions);
		OutResult.Resolution = Component.GetSimulationResolution();
		for (int32 CellY = 0; CellY < OutResult.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < OutResult.Resolution; ++CellX)
			{
				Component.SetTerrainElevationAtCell(CellX, CellY, FloorTopZCm);
			}
		}
		Component.CompleteTerrainElevationSampling();

		for (int32 StepIndex = 0; StepIndex < ContactWarmupSteps; ++StepIndex)
		{
			ArmWorld.StepWorld();
		}
		if (!Component.IsUsingGpuSolver())
		{
			Test.AddError(FString::Printf(TEXT("%s: the water live actor did not select the GPU solver"), *ArmLabel));
			return false;
		}

		for (int32 StepIndex = 0; StepIndex < ContactStampSteps; ++StepIndex)
		{
			for (const FTDFluidBrushStamp& Stamp : Stamps)
			{
				Component.QueueBrushStamp(Stamp);
			}
			ArmWorld.StepWorld();
		}
		for (int32 StepIndex = 0; StepIndex < ContactSettleSteps; ++StepIndex)
		{
			ArmWorld.StepWorld();
		}

		const bool bHasRead = ReadRenderTargetPixels(Component.GetDensityPressureTexture(), OutResult.DensityPixels);
		return Test.TestTrue(*(ArmLabel + TEXT(" read the GPU density output for every cell")), bHasRead && OutResult.DensityPixels.Num() == OutResult.Resolution * OutResult.Resolution);
	}

	float MeasureProbeWhitewater(const FTDContactArmResult& Arm)
	{
		const int32 CenterCell = Arm.Resolution / 2;
		float MaximumWhitewater = 0.0f;
		for (int32 CellY = CenterCell - ProbeHalfSpanCells; CellY < CenterCell + ProbeHalfSpanCells; ++CellY)
		{
			for (int32 CellX = CenterCell - ProbeHalfSpanCells; CellX < CenterCell + ProbeHalfSpanCells; ++CellX)
			{
				MaximumWhitewater = FMath::Max(MaximumWhitewater, Arm.DensityPixels[CellY * Arm.Resolution + CellX].R.GetFloat());
			}
		}
		return MaximumWhitewater;
	}

	struct FTDSplashWhitewaterReading
	{
		bool bHasRead = false;
		float MaximumWhitewater = 0.0f;
		double MaximumDistanceCm = 0.0;
		double NearestDistanceCm = 0.0;
		double FarthestDistanceCm = 0.0;
		int32 NonZeroCellCount = 0;
	};

	FTDSplashWhitewaterReading ReadWhitewaterAroundPoint(const UTDFluidLiveComponent& WaterComponent, const FVector& CenterWorld, double RadiusCm)
	{
		FTDSplashWhitewaterReading Reading;
		TArray<FFloat16Color> Pixels;
		const int32 Resolution = WaterComponent.GetSimulationResolution();
		if (!ReadRenderTargetPixels(WaterComponent.GetDensityPressureTexture(), Pixels) || Pixels.Num() != Resolution * Resolution)
		{
			return Reading;
		}

		Reading.bHasRead = true;
		const FVector WindowCenter = WaterComponent.GetSimulationWindowCenterWorld();
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const float Whitewater = Pixels[CellY * Resolution + CellX].R.GetFloat();
				if (Whitewater <= 0.0f)
				{
					continue;
				}

				const FVector2D CellCenter(WindowCenter.X + (CellX + 0.5 - 0.5 * Resolution) * SceneCellSizeCm, WindowCenter.Y + (CellY + 0.5 - 0.5 * Resolution) * SceneCellSizeCm);
				const double DistanceCm = FVector2D::Distance(CellCenter, FVector2D(CenterWorld.X, CenterWorld.Y));
				if (DistanceCm > RadiusCm)
				{
					continue;
				}

				Reading.NearestDistanceCm = Reading.NonZeroCellCount == 0 ? DistanceCm : FMath::Min(Reading.NearestDistanceCm, DistanceCm);
				Reading.FarthestDistanceCm = FMath::Max(Reading.FarthestDistanceCm, DistanceCm);
				++Reading.NonZeroCellCount;
				if (Whitewater > Reading.MaximumWhitewater)
				{
					Reading.MaximumWhitewater = Whitewater;
					Reading.MaximumDistanceCm = DistanceCm;
				}
			}
		}
		return Reading;
	}

	struct FTDWaterParticleAssets
	{
		TStrongObjectPtr<UNiagaraSystem> DebrisSystem;
		TStrongObjectPtr<UNiagaraSystem> WhitewaterSystem;
		TStrongObjectPtr<UNiagaraSystem> SplashSystem;
		TStrongObjectPtr<UNiagaraDataChannelAsset> ContactChannel;
		TStrongObjectPtr<UStaticMesh> CubeMesh;
	};

	bool ExpectLoaded(FAutomationTestBase& Test, const FString& CaseLabel, const UObject* LoadedObject, const TCHAR* ObjectPath)
	{
		if (LoadedObject)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s: %s is missing; build the W-1b assets (spec-w1b section 6) before this asset group"), *CaseLabel, ObjectPath));
		return false;
	}

	bool PreloadWaterParticleAssets(FAutomationTestBase& Test, const FString& CaseLabel, FTDWaterParticleAssets& OutAssets)
	{
		OutAssets.DebrisSystem.Reset(TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(DebrisSystemPath)).LoadSynchronous());
		OutAssets.WhitewaterSystem.Reset(TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(WhitewaterSystemPath)).LoadSynchronous());
		OutAssets.SplashSystem.Reset(TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(SplashSystemPath)).LoadSynchronous());
		OutAssets.ContactChannel.Reset(TSoftObjectPtr<UNiagaraDataChannelAsset>(FSoftObjectPath(ContactChannelPath)).LoadSynchronous());
		OutAssets.CubeMesh.Reset(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(CubeMeshPath)).LoadSynchronous());
		bool bHasAll = ExpectLoaded(Test, CaseLabel, OutAssets.DebrisSystem.Get(), DebrisSystemPath);
		bHasAll &= ExpectLoaded(Test, CaseLabel, OutAssets.WhitewaterSystem.Get(), WhitewaterSystemPath);
		bHasAll &= ExpectLoaded(Test, CaseLabel, OutAssets.SplashSystem.Get(), SplashSystemPath);
		bHasAll &= ExpectLoaded(Test, CaseLabel, OutAssets.ContactChannel.Get(), ContactChannelPath);
		bHasAll &= ExpectLoaded(Test, CaseLabel, OutAssets.CubeMesh.Get(), CubeMeshPath);
		return bHasAll;
	}

	FTDFluidSimulationConfig MakeSceneWaterConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.SolverBackend = ETDFluidSolverBackend::Gpu;
		Config.GridResolution = SceneResolution;
		Config.SimExtentCm = FVector2D(SceneResolution * SceneCellSizeCm, SceneResolution * SceneCellSizeCm);
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = 0.35f;
		Config.PressureIterations = 12;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 1.0f;
		Config.MaxPointsPerTick = 512;
		Config.bEnableWaterLayer = true;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.AmbientNoiseAmount = 0.0f;
		Config.AmbientDensity = 0.0f;
		Config.RapidsWhitewaterPerSec = 0.0f;
		Config.WhitewaterFromVorticity = 0.0f;
		Config.SwashHeightCm = 0.0f;
		return Config;
	}

	FTDFluidWaterSurfaceParameters MakeSurfaceOverride(float PassiveWaveAmplitudeCm)
	{
		FTDFluidWaterSurfaceParameters Parameters;
		Parameters.BaseHeightCm = WaterSurfaceZCm;
		Parameters.UsesTerrainBase = 0.0f;
		Parameters.TerrainReferenceZCm = FloorTopZCm;
		Parameters.DepthReferenceCm = WaterSurfaceZCm - FloorTopZCm;
		Parameters.RippleHeightReferenceCm = 8.0f;
		Parameters.LayerActive = 1.0f;
		Parameters.BorderFadeRatio = 0.1f;
		Parameters.PassiveWaveAmplitudeCm = PassiveWaveAmplitudeCm;
		Parameters.Coverage = 1.0f;
		Parameters.CameraWorld = FVector(0.0, 0.0, 1000.0);
		return Parameters;
	}

	AActor* SpawnRootedActor(UWorld& SceneWorld, USceneComponent*& OutRoot, TSubclassOf<USceneComponent> RootClass, const FVector& Location)
	{
		AActor* SceneActor = SceneWorld.SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!SceneActor)
		{
			return nullptr;
		}

		OutRoot = NewObject<USceneComponent>(SceneActor, RootClass);
		SceneActor->AddInstanceComponent(OutRoot);
		SceneActor->SetRootComponent(OutRoot);
		OutRoot->RegisterComponent();
		SceneActor->SetActorLocation(Location);
		return SceneActor;
	}

	bool SpawnWaterSceneProps(UWorld& SceneWorld, UStaticMesh& CubeMesh)
	{
		USceneComponent* FloorRoot = nullptr;
		AActor* FloorActor = SpawnRootedActor(SceneWorld, FloorRoot, UStaticMeshComponent::StaticClass(), FVector(0.0, 0.0, FloorTopZCm - CubeHalfSizeCm));
		UStaticMeshComponent* FloorMesh = Cast<UStaticMeshComponent>(FloorRoot);
		if (!FloorActor || !FloorMesh)
		{
			return false;
		}
		FloorMesh->SetMobility(EComponentMobility::Movable);
		FloorMesh->SetStaticMesh(&CubeMesh);
		FloorMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		FloorMesh->SetCollisionObjectType(ECC_WorldStatic);
		FloorMesh->SetCollisionResponseToAllChannels(ECR_Block);
		FloorMesh->SetWorldScale3D(FVector(100.0, 100.0, 1.0));

		constexpr double RegionHalfHeightCm = 50.0;
		USceneComponent* RegionRoot = nullptr;
		AActor* RegionActor = SpawnRootedActor(SceneWorld, RegionRoot, UBoxComponent::StaticClass(), FVector(0.0, 0.0, WaterSurfaceZCm - RegionHalfHeightCm));
		UBoxComponent* RegionBox = Cast<UBoxComponent>(RegionRoot);
		if (!RegionActor || !RegionBox)
		{
			return false;
		}
		RegionBox->SetBoxExtent(FVector(SceneRegionHalfExtentCm, SceneRegionHalfExtentCm, RegionHalfHeightCm));
		RegionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RegionActor->Tags.Add(FName(TEXT("TDFluidWaterRegion")));
		return true;
	}

	struct FTDWaterParticleScene
	{
		FTDWaterParticleRhiWorld SceneWorld;
		ATDFluidWaterActor* WaterActor = nullptr;
		ATDFluidWaterParticleActor* ParticleActor = nullptr;
		int32 ReadyStepCount = 0;

		UTDFluidLiveComponent& GetWaterComponent() const
		{
			return *WaterActor->LiveComponent;
		}

		void Step()
		{
			SceneWorld.StepWorld();
		}
	};

	bool BuildWaterParticleScene(FAutomationTestBase& Test, const FString& SceneLabel, const FTDWaterParticleAssets& Assets, bool bSpawnsParticleActor, float PassiveWaveAmplitudeCm, FTDWaterParticleScene& OutScene)
	{
		UWorld& SceneWorld = *OutScene.SceneWorld.World;
		if (!Test.TestTrue(*(SceneLabel + TEXT(" spawned the floor and the water region")), SpawnWaterSceneProps(SceneWorld, *Assets.CubeMesh.Get())))
		{
			return false;
		}

		USceneComponent* AnchorRoot = nullptr;
		AActor* Anchor = SpawnRootedActor(SceneWorld, AnchorRoot, USceneComponent::StaticClass(), FVector(0.0, 0.0, WaterSurfaceZCm));
		OutScene.WaterActor = Anchor ? SceneWorld.SpawnActor<ATDFluidWaterActor>(ATDFluidWaterActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator) : nullptr;
		if (!Test.TestTrue(*(SceneLabel + TEXT(" spawned the water actor")), OutScene.WaterActor && OutScene.WaterActor->LiveComponent))
		{
			return false;
		}
		OutScene.WaterActor->FollowTargetActor = Anchor;
		OutScene.WaterActor->ViewLookAheadCm = 0.0f;
		OutScene.WaterActor->bSleepWhenNoWaterInWindow = false;
		OutScene.WaterActor->bSampleTerrainElevation = true;
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(&SceneWorld, ETDFluidPresetKind::Sea);
		Preset->bOverridePresetDefaults = true;
		Preset->SimulationConfig = MakeSceneWaterConfig();
		OutScene.WaterActor->InitializeFromPreset(Preset, 0.0f);

		if (bSpawnsParticleActor)
		{
			OutScene.ParticleActor = SceneWorld.SpawnActorDeferred<ATDFluidWaterParticleActor>(ATDFluidWaterParticleActor::StaticClass(), FTransform::Identity);
			if (!Test.TestNotNull(*(SceneLabel + TEXT(" spawned the water particle actor")), OutScene.ParticleActor))
			{
				return false;
			}
			OutScene.ParticleActor->WaterActor = OutScene.WaterActor;
			OutScene.ParticleActor->DebrisSystem = Assets.DebrisSystem.Get();
			OutScene.ParticleActor->WhitewaterSystem = Assets.WhitewaterSystem.Get();
			OutScene.ParticleActor->SplashSystem = Assets.SplashSystem.Get();
			OutScene.ParticleActor->ContactDataChannel = Assets.ContactChannel.Get();
			OutScene.ParticleActor->FinishSpawning(FTransform::Identity);
			OutScene.ParticleActor->SetSurfaceParametersOverrideForTests(MakeSurfaceOverride(PassiveWaveAmplitudeCm));
		}

		OutScene.Step();
		if (!OutScene.GetWaterComponent().IsUsingGpuSolver())
		{
			Test.AddError(FString::Printf(TEXT("%s: the water actor did not select the GPU solver"), *SceneLabel));
			return false;
		}
		for (OutScene.ReadyStepCount = 1; OutScene.ReadyStepCount < SceneReadyStepLimit && OutScene.GetWaterComponent().GetWaterLayerDisplayAlpha() <= 0.0f; ++OutScene.ReadyStepCount)
		{
			OutScene.Step();
		}
		return Test.TestTrue(*(SceneLabel + TEXT(" water layer becomes visible after terrain sampling")), OutScene.GetWaterComponent().GetWaterLayerDisplayAlpha() > 0.0f);
	}

	UNiagaraComponent* WaitForActiveLayer(FTDWaterParticleScene& Scene, bool bWantsDebris)
	{
		for (int32 StepIndex = 0; StepIndex < ComponentReadyStepLimit; ++StepIndex)
		{
			UNiagaraComponent* LayerComponent = bWantsDebris ? Scene.ParticleActor->GetDebrisComponent() : Scene.ParticleActor->GetWhitewaterComponent();
			if (LayerComponent && LayerComponent->IsActive())
			{
				return LayerComponent;
			}
			FlushAsyncLoading();
			Scene.Step();
			FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, StepSeconds);
		}
		return nullptr;
	}

	bool CaptureLayerFrames(FAutomationTestBase& Test, const FString& RunLabel, FTDWaterParticleScene& Scene, UNiagaraComponent& LayerComponent, int32 StepCount, TFunctionRef<void(int32)> BeforeStep, TStrongObjectPtr<UNiagaraSimCache>& OutCache)
	{
		UNiagaraSimCache* SimCache = NewObject<UNiagaraSimCache>(GetTransientPackage());
		OutCache.Reset(SimCache);
		if (!Test.TestTrue(*(RunLabel + TEXT(": begins the sim cache write")), SimCache->BeginWrite(FNiagaraSimCacheCreateParameters(), &LayerComponent)))
		{
			return false;
		}

		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			BeforeStep(StepIndex);
			Scene.Step();
			if (!SimCache->WriteFrame(&LayerComponent))
			{
				SimCache->EndWrite();
				Test.AddError(FString::Printf(TEXT("%s: step %d did not add a cache frame"), *RunLabel, StepIndex));
				return false;
			}
		}
		return Test.TestTrue(*(RunLabel + TEXT(": ends the sim cache write")), SimCache->EndWrite());
	}

	bool AreFinite(const TArray<FVector>& Values)
	{
		for (const FVector& Value : Values)
		{
			if (Value.ContainsNaN() || !FMath::IsFinite(Value.X) || !FMath::IsFinite(Value.Y) || !FMath::IsFinite(Value.Z))
			{
				return false;
			}
		}
		return true;
	}

	double ComputePercentile(TArray<double> Values, double Fraction)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}

		Values.Sort();
		const int32 Index = FMath::Clamp(FMath::CeilToInt32(Fraction * Values.Num()) - 1, 0, Values.Num() - 1);
		return Values[Index];
	}

	double ComputeStandardDeviationZ(const TArray<FVector>& Positions)
	{
		if (Positions.IsEmpty())
		{
			return 0.0;
		}

		double Sum = 0.0;
		for (const FVector& Position : Positions)
		{
			Sum += Position.Z;
		}
		const double Mean = Sum / Positions.Num();
		double SquaredSum = 0.0;
		for (const FVector& Position : Positions)
		{
			SquaredSum += FMath::Square(Position.Z - Mean);
		}
		return FMath::Sqrt(SquaredSum / Positions.Num());
	}

	bool WriteRecord(FAutomationTestBase& Test, const FString& FileName, const TArray<TPair<FString, double>>& Fields)
	{
		const FString RecordPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX"), TEXT("w1b"), FileName);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(RecordPath), true);
		TArray<FString> Lines;
		for (const TPair<FString, double>& Field : Fields)
		{
			const FString ValueText = FMath::IsFinite(Field.Value) ? FString::Printf(TEXT("%.6f"), Field.Value) : FString(TEXT("null"));
			Lines.Add(FString::Printf(TEXT("  \"%s\": %s"), *Field.Key, *ValueText));
		}
		const FString RecordText = FString::Printf(TEXT("{\n%s\n}\n"), *FString::Join(Lines, TEXT(",\n")));
		const bool bHasWritten = FFileHelper::SaveStringToFile(RecordText, *RecordPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		Test.AddInfo(FString::Printf(TEXT("record %s"), *RecordPath));
		return Test.TestTrue(*(FileName + TEXT(" record is written")), bHasWritten);
	}

	bool ReadProbeSamples(FTDWaterParticleScene& Scene, const TArray<FVector>& ProbeLocations, TArray<FTDFluidWaterPointSample>& OutSamples)
	{
		UTDFluidLiveComponent& WaterComponent = Scene.GetWaterComponent();
		WaterComponent.SetWaterProbeLocations(ProbeLocations);
		for (int32 StepIndex = 0; StepIndex < ProbeArrivalStepLimit; ++StepIndex)
		{
			Scene.Step();
			if (WaterComponent.GetWaterProbeSamples(OutSamples) && OutSamples.Num() == ProbeLocations.Num())
			{
				return true;
			}
		}
		return false;
	}

	struct FTDDebrisFollowArm
	{
		TArray<FVector> Positions;
		TArray<double> ProbeOffsetsCm;
	};

	bool RunDebrisFollowArm(FAutomationTestBase& Test, const FString& ArmLabel, const FTDWaterParticleAssets& Assets, float PassiveWaveAmplitudeCm, FTDDebrisFollowArm& OutArm)
	{
		FTDWaterParticleScene Scene;
		if (!BuildWaterParticleScene(Test, ArmLabel, Assets, true, PassiveWaveAmplitudeCm, Scene))
		{
			return false;
		}

		UNiagaraComponent* DebrisComponent = WaitForActiveLayer(Scene, true);
		if (!Test.TestNotNull(*(ArmLabel + TEXT(" debris layer activates")), DebrisComponent))
		{
			return false;
		}

		TStrongObjectPtr<UNiagaraSimCache> SimCache;
		if (!CaptureLayerFrames(Test, ArmLabel, Scene, *DebrisComponent, DebrisFollowSteps, [](int32) {}, SimCache))
		{
			return false;
		}
		SimCache->ReadPositionAttribute(OutArm.Positions, PositionAttributeName, DebrisEmitterName, true, SimCache->GetNumFrames() - 1);

		TArray<FVector> ProbeLocations;
		for (int32 ParticleIndex = 0; ParticleIndex < FMath::Min(DebrisProbeCount, OutArm.Positions.Num()); ++ParticleIndex)
		{
			ProbeLocations.Add(OutArm.Positions[ParticleIndex]);
		}
		TArray<FTDFluidWaterPointSample> Samples;
		if (!ProbeLocations.IsEmpty() && ReadProbeSamples(Scene, ProbeLocations, Samples))
		{
			for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
			{
				OutArm.ProbeOffsetsCm.Add(ProbeLocations[SampleIndex].Z - (Samples[SampleIndex].WaterSurfaceHeightCm + Samples[SampleIndex].RippleHeightCm));
			}
		}
		return true;
	}

	UNiagaraComponent* FindActiveSplashComponent(const UWorld& SceneWorld, const UNiagaraSystem& SplashSystem)
	{
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			if (It->GetWorld() == &SceneWorld && It->GetFXSystemAsset() == &SplashSystem && It->IsActive())
			{
				return *It;
			}
		}
		return nullptr;
	}

	FTDFluidWaterSplashEvent MakeEntrySplashEvent(const FVector2D& PlanePoint)
	{
		FTDFluidWaterSplashEvent SplashEvent;
		SplashEvent.WorldLocation = FVector(PlanePoint.X, PlanePoint.Y, WaterSurfaceZCm);
		SplashEvent.ImpactVelocity = FVector(0.0, 0.0, -SplashImpactSpeedCmPerSec);
		SplashEvent.ImpactSpeedCmPerSec = SplashImpactSpeedCmPerSec;
		SplashEvent.RadiusCm = SplashImpactRadiusCm;
		SplashEvent.SurfaceHeightCm = WaterSurfaceZCm;
		SplashEvent.Kind = ETDFluidWaterSplashKind::Entry;
		return SplashEvent;
	}

	TArray<FTDFluidBrushStamp> MakeLeftHalfWhitewaterStamps()
	{
		TArray<FTDFluidBrushStamp> Stamps;
		for (double StampX = WhitewaterStampMinX; StampX <= WhitewaterStampMaxX; StampX += 200.0)
		{
			for (double StampY = -1200.0; StampY <= 1200.0; StampY += 200.0)
			{
				FTDFluidBrushStamp& Stamp = Stamps.AddDefaulted_GetRef();
				Stamp.WorldLocation = FVector(StampX, StampY, WaterSurfaceZCm);
				Stamp.RadiusCm = 150.0f;
				Stamp.DensityStrength = 0.9f;
				Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
			}
		}
		return Stamps;
	}

	TArray<FTDFluidBrushStamp> MakeFlowStamps()
	{
		TArray<FTDFluidBrushStamp> Stamps;
		for (double StampX = -1250.0; StampX <= 1250.0; StampX += 500.0)
		{
			for (double StampY = -1250.0; StampY <= 1250.0; StampY += 500.0)
			{
				FTDFluidBrushStamp& Stamp = Stamps.AddDefaulted_GetRef();
				Stamp.WorldLocation = FVector(StampX, StampY, WaterSurfaceZCm);
				Stamp.WorldVelocity = FVector(FlowSpeedCmPerSec, 0.0, 0.0);
				Stamp.RadiusCm = 400.0f;
				Stamp.VelocityStrength = 1.0f;
				Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
			}
		}
		return Stamps;
	}

	bool ReadOutputChecksums(FAutomationTestBase& Test, const FString& ArmLabel, const UTDFluidLiveComponent& WaterComponent, TArray<uint32>& OutChecksums)
	{
		OutChecksums.Reset();
		UTexture* const OutputTextures[] = { WaterComponent.GetDensityPressureTexture(), WaterComponent.GetVelocityTexture(), WaterComponent.GetWaterSurfaceOutputTexture() };
		for (UTexture* OutputTexture : OutputTextures)
		{
			TArray<FFloat16Color> Pixels;
			if (!ReadRenderTargetPixels(OutputTexture, Pixels))
			{
				Test.AddError(FString::Printf(TEXT("%s: output texture %d could not be read back"), *ArmLabel, OutChecksums.Num()));
				return false;
			}
			OutChecksums.Add(FCrc::MemCrc32(Pixels.GetData(), Pixels.Num() * sizeof(FFloat16Color)));
		}
		return true;
	}

	bool RunOutputOnlyArm(FAutomationTestBase& Test, const FString& ArmLabel, const FTDWaterParticleAssets& Assets, bool bSpawnsParticleActor, int32& OutReadyStepCount, TArray<uint32>& OutChecksums)
	{
		FTDWaterParticleScene Scene;
		if (!BuildWaterParticleScene(Test, ArmLabel, Assets, bSpawnsParticleActor, 0.0f, Scene))
		{
			return false;
		}

		OutReadyStepCount = Scene.ReadyStepCount;
		for (int32 StepIndex = 0; StepIndex < OutputOnlySteps; ++StepIndex)
		{
			Scene.Step();
		}
		if (bSpawnsParticleActor && !Test.TestTrue(*(ArmLabel + TEXT(" debris layer ran during the water steps")), Scene.ParticleActor->GetDebrisComponent() != nullptr))
		{
			return false;
		}
		return ReadOutputChecksums(Test, ArmLabel, Scene.GetWaterComponent(), OutChecksums);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleRhiContactRaisesWhitewaterGpuTest,
	"TDGame.FluidFXRhi.WaterParticle.ContactRaisesWhitewaterGpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleRhiContactRaisesWhitewaterGpuTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.WaterParticle.ContactRaisesWhitewaterGpu");
	FTDWaterGpuSelectionScope GpuScope;
	if (!CanRunWaterParticleRhiTest(*this, CaseLabel, GpuScope))
	{
		return false;
	}

	const TArray<FTDFluidBrushStamp> ContactStamps = MakeContactStamps(MakeContactGpuConfig());
	const FTDFluidWaterParticleSettings Settings = FTDFluidWaterParticleSettings();
	if (!TestEqual(TEXT("The contact points fill the default contact stamp budget"), ContactStamps.Num(), Settings.MaxContactStampsPerTick) || ContactStamps.IsEmpty())
	{
		return false;
	}

	FTDContactArmResult ControlArm;
	FTDContactArmResult ContactArm;
	FTDContactArmResult RepeatArm;
	if (!RunContactArm(*this, CaseLabel + TEXT(" control"), TArray<FTDFluidBrushStamp>(), ControlArm)
		|| !RunContactArm(*this, CaseLabel + TEXT(" contact"), ContactStamps, ContactArm)
		|| !RunContactArm(*this, CaseLabel + TEXT(" repeat"), ContactStamps, RepeatArm))
	{
		return false;
	}

	const float ControlProbe = MeasureProbeWhitewater(ControlArm);
	const float ContactProbe = MeasureProbeWhitewater(ContactArm);
	const float RepeatProbe = MeasureProbeWhitewater(RepeatArm);
	ReportLine(*this, FString::Printf(TEXT("TDFluidWaterParticleContact: %s stamps=%d probe_contact=%.5f probe_control=%.5f probe_repeat=%.5f"), *CaseLabel, ContactStamps.Num(), ContactProbe, ControlProbe, RepeatProbe));
	TestTrue(*FString::Printf(TEXT("GPU whitewater at the contact probe rises above zero (%.5f)"), ContactProbe), ContactProbe > 0.0f);
	TestTrue(*FString::Printf(TEXT("GPU whitewater at the control probe stays zero (%.5f)"), ControlProbe), ControlProbe == 0.0f);
	TestTrue(TEXT("Two contact runs give the same probe whitewater"), FMemory::Memcmp(&ContactProbe, &RepeatProbe, sizeof(float)) == 0);
	TestTrue(TEXT("Two contact runs give bit-identical density outputs"), ContactArm.DensityPixels.Num() == RepeatArm.DensityPixels.Num()
		&& FMemory::Memcmp(ContactArm.DensityPixels.GetData(), RepeatArm.DensityPixels.GetData(), ContactArm.DensityPixels.Num() * sizeof(FFloat16Color)) == 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleRhiDebrisFollowsSurfaceTest,
	"TDGame.FluidFXRhi.WaterParticleAssets.DebrisFollowsSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleRhiDebrisFollowsSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.WaterParticleAssets.DebrisFollowsSurface");
	FTDWaterGpuSelectionScope GpuScope;
	FTDWaterParticleAssets Assets;
	if (!CanRunWaterParticleRhiTest(*this, CaseLabel, GpuScope) || !PreloadWaterParticleAssets(*this, CaseLabel, Assets))
	{
		return false;
	}

	FTDDebrisFollowArm StillArm;
	FTDDebrisFollowArm WaveArm;
	if (!RunDebrisFollowArm(*this, CaseLabel + TEXT(" still"), Assets, 0.0f, StillArm) || !RunDebrisFollowArm(*this, CaseLabel + TEXT(" wave"), Assets, WaveAmplitudeCm, WaveArm))
	{
		return false;
	}

	TestTrue(TEXT("Still debris positions are finite"), AreFinite(StillArm.Positions));
	TestTrue(TEXT("Wave debris positions are finite"), AreFinite(WaveArm.Positions));
	TestTrue(*FString::Printf(TEXT("Still water keeps at least %d debris after 4 seconds (%d)"), DebrisMinimumCount, StillArm.Positions.Num()), StillArm.Positions.Num() >= DebrisMinimumCount);
	TestTrue(TEXT("Every debris probe returned a sample"), StillArm.ProbeOffsetsCm.Num() == FMath::Min(DebrisProbeCount, StillArm.Positions.Num()));

	const double MedianOffsetCm = ComputePercentile(StillArm.ProbeOffsetsCm, 0.5);
	TArray<double> OffsetSpreadCm;
	for (const double OffsetCm : StillArm.ProbeOffsetsCm)
	{
		OffsetSpreadCm.Add(FMath::Abs(OffsetCm - MedianOffsetCm));
	}
	const double StillStandardDeviationCm = ComputeStandardDeviationZ(StillArm.Positions);
	const double WaveStandardDeviationCm = ComputeStandardDeviationZ(WaveArm.Positions);
	double WaveMaximumHeightCm = 0.0;
	for (const FVector& Position : WaveArm.Positions)
	{
		WaveMaximumHeightCm = FMath::Max(WaveMaximumHeightCm, FMath::Abs(Position.Z - WaterSurfaceZCm));
	}
	TestTrue(*FString::Printf(TEXT("Passive waves widen the debris height spread (%.3f > %.3f)"), WaveStandardDeviationCm, StillStandardDeviationCm), WaveStandardDeviationCm > StillStandardDeviationCm);

	TArray<TPair<FString, double>> Fields;
	Fields.Add({ TEXT("still_count"), static_cast<double>(StillArm.Positions.Num()) });
	Fields.Add({ TEXT("still_probe_count"), static_cast<double>(StillArm.ProbeOffsetsCm.Num()) });
	Fields.Add({ TEXT("still_offset_median_cm"), MedianOffsetCm });
	Fields.Add({ TEXT("still_offset_abs_p95_cm"), ComputePercentile([&StillArm]() { TArray<double> Absolute; for (const double OffsetCm : StillArm.ProbeOffsetsCm) { Absolute.Add(FMath::Abs(OffsetCm)); } return Absolute; }(), 0.95) });
	Fields.Add({ TEXT("still_offset_spread_p95_cm"), ComputePercentile(OffsetSpreadCm, 0.95) });
	Fields.Add({ TEXT("still_z_stddev_cm"), StillStandardDeviationCm });
	Fields.Add({ TEXT("wave_amplitude_cm"), static_cast<double>(WaveAmplitudeCm) });
	Fields.Add({ TEXT("wave_count"), static_cast<double>(WaveArm.Positions.Num()) });
	Fields.Add({ TEXT("wave_z_stddev_cm"), WaveStandardDeviationCm });
	Fields.Add({ TEXT("wave_z_max_abs_cm"), WaveMaximumHeightCm });
	WriteRecord(*this, TEXT("debris-follows-surface.json"), Fields);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleRhiDebrisCarriedByFlowTest,
	"TDGame.FluidFXRhi.WaterParticleAssets.DebrisCarriedByFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleRhiDebrisCarriedByFlowTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.WaterParticleAssets.DebrisCarriedByFlow");
	FTDWaterGpuSelectionScope GpuScope;
	FTDWaterParticleAssets Assets;
	FTDWaterParticleScene Scene;
	if (!CanRunWaterParticleRhiTest(*this, CaseLabel, GpuScope) || !PreloadWaterParticleAssets(*this, CaseLabel, Assets) || !BuildWaterParticleScene(*this, CaseLabel, Assets, true, 0.0f, Scene))
	{
		return false;
	}

	UNiagaraComponent* DebrisComponent = WaitForActiveLayer(Scene, true);
	if (!TestNotNull(TEXT("Debris layer activates"), DebrisComponent))
	{
		return false;
	}

	const TArray<FTDFluidBrushStamp> FlowStamps = MakeFlowStamps();
	UTDFluidLiveComponent& WaterComponent = Scene.GetWaterComponent();
	TStrongObjectPtr<UNiagaraSimCache> SimCache;
	if (!CaptureLayerFrames(*this, CaseLabel, Scene, *DebrisComponent, FlowSteps, [&FlowStamps, &WaterComponent](int32)
	{
		for (const FTDFluidBrushStamp& Stamp : FlowStamps)
		{
			WaterComponent.QueueBrushStamp(Stamp);
		}
	}, SimCache))
	{
		return false;
	}

	TArray<FVector> Velocities;
	SimCache->ReadVectorAttribute(Velocities, VelocityAttributeName, DebrisEmitterName, SimCache->GetNumFrames() - 1);
	if (!TestTrue(TEXT("Debris velocities exist and are finite"), !Velocities.IsEmpty() && AreFinite(Velocities)))
	{
		return false;
	}

	double SumVelocityX = 0.0;
	double SumSpeed = 0.0;
	for (const FVector& Velocity : Velocities)
	{
		SumVelocityX += Velocity.X;
		SumSpeed += Velocity.Size2D();
	}
	const double MeanVelocityX = SumVelocityX / Velocities.Num();
	TestTrue(*FString::Printf(TEXT("Debris drifts along the +X flow (mean vx %.2f)"), MeanVelocityX), MeanVelocityX > 0.0);

	TArray<TPair<FString, double>> Fields;
	Fields.Add({ TEXT("count"), static_cast<double>(Velocities.Num()) });
	Fields.Add({ TEXT("mean_velocity_x_cm_per_sec"), MeanVelocityX });
	Fields.Add({ TEXT("mean_speed_cm_per_sec"), SumSpeed / Velocities.Num() });
	Fields.Add({ TEXT("speed_ratio_to_flow"), MeanVelocityX / FlowSpeedCmPerSec });
	WriteRecord(*this, TEXT("debris-carried-by-flow.json"), Fields);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleRhiSplashContactReachesWaterTest,
	"TDGame.FluidFXRhi.WaterParticleAssets.SplashContactReachesWater",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleRhiSplashContactReachesWaterTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.WaterParticleAssets.SplashContactReachesWater");
	FTDWaterGpuSelectionScope GpuScope;
	FTDWaterParticleAssets Assets;
	FTDWaterParticleScene Scene;
	if (!CanRunWaterParticleRhiTest(*this, CaseLabel, GpuScope) || !PreloadWaterParticleAssets(*this, CaseLabel, Assets) || !BuildWaterParticleScene(*this, CaseLabel, Assets, true, 0.0f, Scene))
	{
		return false;
	}

	const FTDFluidWaterSplashEvent SplashEvent = MakeEntrySplashEvent(FVector2D::ZeroVector);
	TArray<FVector> ProbeLocations;
	ProbeLocations.Add(SplashEvent.WorldLocation);
	Scene.GetWaterComponent().SetWaterProbeLocations(ProbeLocations);
	if (!TestTrue(TEXT("The entry splash spawns"), Scene.ParticleActor->SpawnSplashEvent(SplashEvent)))
	{
		return false;
	}

	UNiagaraComponent* SplashComponent = FindActiveSplashComponent(*Scene.SceneWorld.World, *Assets.SplashSystem.Get());
	if (!TestNotNull(TEXT("The splash component is active"), SplashComponent))
	{
		return false;
	}

	int32 EarlyParticleCount = 0;
	TStrongObjectPtr<UNiagaraSimCache> SimCache;
	if (CaptureLayerFrames(*this, CaseLabel + TEXT(" early"), Scene, *SplashComponent, SplashEarlySteps, [](int32) {}, SimCache))
	{
		const int32 EmitterIndex = SimCache->GetEmitterIndex(SplashEmitterName);
		for (int32 FrameIndex = 0; EmitterIndex != INDEX_NONE && FrameIndex < SimCache->GetNumFrames(); ++FrameIndex)
		{
			EarlyParticleCount = FMath::Max(EarlyParticleCount, SimCache->GetEmitterNumInstances(EmitterIndex, FrameIndex));
		}
	}
	TestTrue(*FString::Printf(TEXT("Splash particles appear within 0.1 seconds (%d)"), EarlyParticleCount), EarlyParticleCount > 0);

	int32 FirstContactStep = INDEX_NONE;
	int32 FirstWhitewaterStep = INDEX_NONE;
	int32 MaximumContactPoints = 0;
	float MaximumProbeWhitewater = 0.0f;
	bool bHasReadWhitewater = false;
	FTDSplashWhitewaterReading PeakReading;
	TArray<FTDFluidWaterPointSample> Samples;
	for (int32 StepIndex = 0; StepIndex < SplashContactSteps; ++StepIndex)
	{
		Scene.Step();
		const int32 ContactPoints = Scene.ParticleActor->GetLastContactPointCount();
		MaximumContactPoints = FMath::Max(MaximumContactPoints, ContactPoints);
		FirstContactStep = FirstContactStep == INDEX_NONE && ContactPoints > 0 ? StepIndex : FirstContactStep;
		if (Scene.GetWaterComponent().GetWaterProbeSamples(Samples) && Samples.Num() == 1)
		{
			MaximumProbeWhitewater = FMath::Max(MaximumProbeWhitewater, Samples[0].Whitewater);
		}
		if (FirstContactStep == INDEX_NONE)
		{
			continue;
		}

		const FTDSplashWhitewaterReading Reading = ReadWhitewaterAroundPoint(Scene.GetWaterComponent(), SplashEvent.WorldLocation, SplashWhitewaterReadRadiusCm);
		bHasReadWhitewater |= Reading.bHasRead;
		FirstWhitewaterStep = FirstWhitewaterStep == INDEX_NONE && Reading.MaximumWhitewater > 0.0f ? StepIndex : FirstWhitewaterStep;
		PeakReading = Reading.MaximumWhitewater > PeakReading.MaximumWhitewater ? Reading : PeakReading;
	}
	for (int32 StepIndex = 0; StepIndex < SplashTailSteps; ++StepIndex)
	{
		Scene.Step();
	}

	TestTrue(*FString::Printf(TEXT("Splash contact points reach the reader within 2 seconds (max %d)"), MaximumContactPoints), MaximumContactPoints > 0);
	TestTrue(TEXT("The GPU density output is readable once contact points arrive"), bHasReadWhitewater);
	TestTrue(*FString::Printf(TEXT("Whitewater rises within %.0f cm of the splash entry within 2 seconds (%.5f)"), SplashWhitewaterReadRadiusCm, PeakReading.MaximumWhitewater), PeakReading.MaximumWhitewater > 0.0f);
	TestEqual(TEXT("No splash stays active one second after the contact window"), Scene.ParticleActor->GetActiveSplashCount(), 0);

	TArray<TPair<FString, double>> Fields;
	Fields.Add({ TEXT("early_particle_count"), static_cast<double>(EarlyParticleCount) });
	Fields.Add({ TEXT("max_contact_points"), static_cast<double>(MaximumContactPoints) });
	Fields.Add({ TEXT("contact_latency_seconds"), FirstContactStep == INDEX_NONE ? std::numeric_limits<double>::quiet_NaN() : (SplashEarlySteps + FirstContactStep + 1) * static_cast<double>(StepSeconds) });
	Fields.Add({ TEXT("whitewater_latency_seconds"), FirstWhitewaterStep == INDEX_NONE ? std::numeric_limits<double>::quiet_NaN() : (SplashEarlySteps + FirstWhitewaterStep + 1) * static_cast<double>(StepSeconds) });
	Fields.Add({ TEXT("max_probe_whitewater"), static_cast<double>(MaximumProbeWhitewater) });
	Fields.Add({ TEXT("max_whitewater_near_entry"), static_cast<double>(PeakReading.MaximumWhitewater) });
	Fields.Add({ TEXT("max_whitewater_distance_cm"), PeakReading.MaximumDistanceCm });
	Fields.Add({ TEXT("nearest_whitewater_distance_cm"), PeakReading.NearestDistanceCm });
	Fields.Add({ TEXT("farthest_whitewater_distance_cm"), PeakReading.FarthestDistanceCm });
	Fields.Add({ TEXT("nonzero_whitewater_cells"), static_cast<double>(PeakReading.NonZeroCellCount) });
	WriteRecord(*this, TEXT("contact-latency.json"), Fields);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleRhiWhitewaterSpawnsOnWhitewaterTest,
	"TDGame.FluidFXRhi.WaterParticleAssets.WhitewaterSpawnsOnWhitewater",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleRhiWhitewaterSpawnsOnWhitewaterTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.WaterParticleAssets.WhitewaterSpawnsOnWhitewater");
	FTDWaterGpuSelectionScope GpuScope;
	FTDWaterParticleAssets Assets;
	FTDWaterParticleScene Scene;
	if (!CanRunWaterParticleRhiTest(*this, CaseLabel, GpuScope) || !PreloadWaterParticleAssets(*this, CaseLabel, Assets) || !BuildWaterParticleScene(*this, CaseLabel, Assets, true, 0.0f, Scene))
	{
		return false;
	}

	UNiagaraComponent* WhitewaterComponent = WaitForActiveLayer(Scene, false);
	if (!TestNotNull(TEXT("Whitewater layer activates"), WhitewaterComponent))
	{
		return false;
	}

	const TArray<FTDFluidBrushStamp> WhitewaterStamps = MakeLeftHalfWhitewaterStamps();
	UTDFluidLiveComponent& WaterComponent = Scene.GetWaterComponent();
	TStrongObjectPtr<UNiagaraSimCache> SimCache;
	if (!CaptureLayerFrames(*this, CaseLabel, Scene, *WhitewaterComponent, WhitewaterSteps, [&WhitewaterStamps, &WaterComponent](int32)
	{
		for (const FTDFluidBrushStamp& Stamp : WhitewaterStamps)
		{
			WaterComponent.QueueBrushStamp(Stamp);
		}
	}, SimCache))
	{
		return false;
	}

	TArray<FVector> Positions;
	SimCache->ReadPositionAttribute(Positions, PositionAttributeName, WhitewaterEmitterName, true, SimCache->GetNumFrames() - 1);
	int32 LeftCount = 0;
	int32 RightCount = 0;
	for (const FVector& Position : Positions)
	{
		LeftCount += Position.X < 0.0 ? 1 : 0;
		RightCount += Position.X > 0.0 ? 1 : 0;
	}
	ReportLine(*this, FString::Printf(TEXT("TDFluidWaterParticleWhitewater: %s left=%d right=%d"), *CaseLabel, LeftCount, RightCount));
	TestTrue(TEXT("Whitewater positions are finite"), AreFinite(Positions));
	TestTrue(*FString::Printf(TEXT("Whitewater particles spawn on the whitewater half (%d)"), LeftCount), LeftCount > 0);
	TestEqual(TEXT("No whitewater particle spawns on the calm half"), RightCount, 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleRhiOffscreenCostTest,
	"TDGame.FluidFXRhi.WaterParticleAssets.OffscreenCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleRhiOffscreenCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.WaterParticleAssets.OffscreenCost");
#if WITH_NIAGARA_GPU_PROFILER
	FTDWaterGpuSelectionScope GpuScope;
	FTDWaterParticleAssets Assets;
	FTDWaterParticleScene Scene;
	if (!CanRunWaterParticleRhiTest(*this, CaseLabel, GpuScope) || !PreloadWaterParticleAssets(*this, CaseLabel, Assets) || !BuildWaterParticleScene(*this, CaseLabel, Assets, true, 0.0f, Scene))
	{
		return false;
	}

	UNiagaraComponent* DebrisComponent = WaitForActiveLayer(Scene, true);
	UNiagaraComponent* WhitewaterComponent = WaitForActiveLayer(Scene, false);
	if (!TestTrue(TEXT("Debris and whitewater layers activate"), DebrisComponent && WhitewaterComponent))
	{
		return false;
	}

	const TArray<FTDFluidBrushStamp> WhitewaterStamps = MakeLeftHalfWhitewaterStamps();
	const UNiagaraSystem* SplashSystem = Assets.SplashSystem.Get();
	TWeakObjectPtr<UNiagaraComponent> WeakDebris(DebrisComponent);
	TWeakObjectPtr<UNiagaraComponent> WeakWhitewater(WhitewaterComponent);
	double FrameDebrisMicroseconds = 0.0;
	double FrameWhitewaterMicroseconds = 0.0;
	double FrameSplashMicroseconds = 0.0;
	FNiagaraGpuProfilerListener Listener;
	Listener.SetHandler([&](const FNiagaraGpuFrameResultsPtr& Results)
	{
		if (!Results.IsValid())
		{
			return;
		}

		for (const FNiagaraGpuFrameResults::FDispatchResults& Dispatch : Results->DispatchResults)
		{
			const UNiagaraComponent* OwnerComponent = Cast<UNiagaraComponent>(Dispatch.OwnerComponent.Get());
			const double DispatchMicroseconds = static_cast<double>(Dispatch.DurationMicroseconds);
			if (OwnerComponent && OwnerComponent == WeakDebris.Get())
			{
				FrameDebrisMicroseconds += DispatchMicroseconds;
			}
			else if (OwnerComponent && OwnerComponent == WeakWhitewater.Get())
			{
				FrameWhitewaterMicroseconds += DispatchMicroseconds;
			}
			else if (OwnerComponent && OwnerComponent->GetFXSystemAsset() == SplashSystem)
			{
				FrameSplashMicroseconds += DispatchMicroseconds;
			}
		}
	});

	TArray<double> DebrisMicroseconds;
	TArray<double> WhitewaterMicroseconds;
	TArray<double> SplashMicroseconds;
	TArray<double> GameThreadMilliseconds;
	Listener.SetEnabled(true);
	for (int32 FrameIndex = 0; FrameIndex < CostFrameCount; ++FrameIndex)
	{
		for (int32 SplashIndex = Scene.ParticleActor->GetActiveSplashCount(); SplashIndex < CostSplashCount; ++SplashIndex)
		{
			const double Angle = SplashIndex * UE_DOUBLE_TWO_PI / CostSplashCount;
			Scene.ParticleActor->SpawnSplashEvent(MakeEntrySplashEvent(FVector2D(600.0 * FMath::Cos(Angle), 600.0 * FMath::Sin(Angle))));
		}
		for (const FTDFluidBrushStamp& Stamp : WhitewaterStamps)
		{
			Scene.GetWaterComponent().QueueBrushStamp(Stamp);
		}
		FrameDebrisMicroseconds = 0.0;
		FrameWhitewaterMicroseconds = 0.0;
		FrameSplashMicroseconds = 0.0;
		const double FrameStartSeconds = FPlatformTime::Seconds();
		Scene.Step();
		GameThreadMilliseconds.Add((FPlatformTime::Seconds() - FrameStartSeconds) * 1000.0);
		DebrisMicroseconds.Add(FrameDebrisMicroseconds);
		WhitewaterMicroseconds.Add(FrameWhitewaterMicroseconds);
		SplashMicroseconds.Add(FrameSplashMicroseconds);
	}
	Listener.SetEnabled(false);

	const auto ComputeMean = [](const TArray<double>& Values)
	{
		double Sum = 0.0;
		for (const double Value : Values)
		{
			Sum += Value;
		}
		return Values.IsEmpty() ? std::numeric_limits<double>::quiet_NaN() : Sum / Values.Num();
	};
	const double DebrisMean = ComputeMean(DebrisMicroseconds);
	const double WhitewaterMean = ComputeMean(WhitewaterMicroseconds);
	const double SplashMean = ComputeMean(SplashMicroseconds);
	const double GameThreadMean = ComputeMean(GameThreadMilliseconds);

	TArray<TPair<FString, double>> Fields;
	Fields.Add({ TEXT("frames"), static_cast<double>(CostFrameCount) });
	Fields.Add({ TEXT("concurrent_splash_target"), static_cast<double>(CostSplashCount) });
	Fields.Add({ TEXT("debris_gpu_us_mean"), DebrisMean });
	Fields.Add({ TEXT("whitewater_gpu_us_mean"), WhitewaterMean });
	Fields.Add({ TEXT("splash_gpu_us_mean"), SplashMean });
	Fields.Add({ TEXT("game_thread_frame_ms_mean_upper_bound"), GameThreadMean });
	Fields.Add({ TEXT("max_contact_points_per_tick_setting"), static_cast<double>(Scene.ParticleActor->GetSettings().MaxContactPointsPerTick) });
	WriteRecord(*this, TEXT("cost-water-particles.json"), Fields);
	TestTrue(TEXT("Every recorded cost is finite"), FMath::IsFinite(DebrisMean) && FMath::IsFinite(WhitewaterMean) && FMath::IsFinite(SplashMean) && FMath::IsFinite(GameThreadMean));
	return !HasAnyErrors();
#else
	AddError(FString::Printf(TEXT("%s needs WITH_NIAGARA_GPU_PROFILER"), *CaseLabel));
	return false;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterParticleRhiParticlesAreOutputOnlyTest,
	"TDGame.FluidFXRhi.WaterParticleAssets.ParticlesAreOutputOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterParticleRhiParticlesAreOutputOnlyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterParticleRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.WaterParticleAssets.ParticlesAreOutputOnly");
	FTDWaterGpuSelectionScope GpuScope;
	FTDWaterParticleAssets Assets;
	if (!CanRunWaterParticleRhiTest(*this, CaseLabel, GpuScope) || !PreloadWaterParticleAssets(*this, CaseLabel, Assets))
	{
		return false;
	}

	int32 WithReadySteps = 0;
	int32 WithoutReadySteps = 0;
	TArray<uint32> WithChecksums;
	TArray<uint32> WithoutChecksums;
	if (!RunOutputOnlyArm(*this, CaseLabel + TEXT(" with particles"), Assets, true, WithReadySteps, WithChecksums)
		|| !RunOutputOnlyArm(*this, CaseLabel + TEXT(" without particles"), Assets, false, WithoutReadySteps, WithoutChecksums))
	{
		return false;
	}

	ReportLine(*this, FString::Printf(TEXT("TDFluidWaterParticleOutputOnly: %s ready_steps=%d/%d"), *CaseLabel, WithReadySteps, WithoutReadySteps));
	TestEqual(TEXT("Both arms reach the visible water layer on the same step"), WithReadySteps, WithoutReadySteps);
	const TCHAR* const OutputNames[] = { TEXT("density"), TEXT("velocity"), TEXT("surface") };
	for (int32 OutputIndex = 0; OutputIndex < WithChecksums.Num() && OutputIndex < WithoutChecksums.Num(); ++OutputIndex)
	{
		TestEqual(*FString::Printf(TEXT("The %s output hash is unchanged by the particle actor"), OutputNames[OutputIndex]), WithChecksums[OutputIndex], WithoutChecksums[OutputIndex]);
	}
	return !HasAnyErrors();
}

#endif
