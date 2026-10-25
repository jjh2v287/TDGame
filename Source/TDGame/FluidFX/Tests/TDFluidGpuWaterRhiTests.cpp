#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidRiverActor.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterActor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PixelFormat.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderGraphDefinitions.h"
#include "RenderTargetPool.h"
#include "RenderingThread.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidGpuWaterRhiTests, Log, All);

namespace TDFluidGpuWaterRhiTestPrivate
{
	constexpr float StepSeconds60 = 1.0f / 60.0f;
	constexpr float StepSeconds144 = 1.0f / 144.0f;
	constexpr float LargeStepSeconds = 0.1f;
	constexpr int32 ParityResolution = 192;
	constexpr double ParityExtentCm = 3600.0;
	constexpr double ParityCellSizeCm = ParityExtentCm / ParityResolution;
	constexpr int32 OracleResolution = 192;
	constexpr float FloorTopZCm = -100.0f;
	constexpr float WaterSurfaceZCm = 0.0f;
	constexpr float CubeHalfSizeCm = 50.0f;
	constexpr int32 TerrainWaitTickLimit = 900;
	constexpr int32 DischargeStepCount60 = 3600;
	constexpr int32 DischargeStepCount144 = 8640;
	constexpr int32 DischargeSectionColumns[] = { 48, 96, 144 };
	constexpr double DischargeParityTolerance = 0.03;
	constexpr double ReverseFluxParityMargin = 0.01;
	constexpr double GuideWeightThreshold = 0.5;
	constexpr double SolidBandDistanceCm = 28.125;
	constexpr float UnknownTerrainEncoding = 8.0f;
	constexpr float DefaultRippleReferenceCm = 8.0f;
	constexpr float EncodingTolerance = 1.0e-3f;
	constexpr int64 PoolUsageToleranceBytes = 64 * 1024;
	constexpr float UnitRangeSlack = 1.0e-4f;
	const TCHAR* const WaterGpuVariableName = TEXT("TD.FluidFX.WaterGpu");
	const TCHAR* const SolverBackendVariableName = TEXT("TD.FluidFX.SolverBackend");
	const TCHAR* const WaterVelocityFormatVariableName = TEXT("TD.FluidFX.WaterGpuVelocityFloat32");
	const TCHAR* const WaterRippleFormatVariableName = TEXT("TD.FluidFX.WaterGpuRippleFloat32");
	const TCHAR* const WaterPressureModeVariableName = TEXT("TD.FluidFX.WaterGpuPressureMode");
	const TCHAR* const GpuPressureSolverVariableName = TEXT("TD.FluidFX.GpuPressureSolver");
	const TCHAR* const GpuPressureCyclesVariableName = TEXT("TD.FluidFX.GpuPressureCycles");
	const TCHAR* const CaptureBaselineEnvironmentName = TEXT("FX4B_CAPTURE_BASELINE");
	const TCHAR* const DiagnosticFloorsEnvironmentName = TEXT("FX4B_DIAGNOSTIC_FLOORS");
	const TCHAR* const CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	FName GetWaterSurfaceTag()
	{
		return FName(TEXT("TDFluidWaterSurface"));
	}

	FName GetWaterRegionTag()
	{
		return FName(TEXT("TDFluidWaterRegion"));
	}

	FName GetWaterObstacleTag()
	{
		return FName(TEXT("TDFluidObstacle"));
	}

	FString MakeMissingWaterGpuMessage(const FString& Label)
	{
		return FString::Printf(TEXT("%s: TD.FluidFX.WaterGpu is not registered, so the water layer still forces the CPU solver (FX-4b not implemented)"), *Label);
	}

	FString MakeGpuNotSelectedMessage(const FString& Label)
	{
		return FString::Printf(TEXT("%s: the GPU water actor did not select the GPU solver (water layer forces CPU before FX-4b)"), *Label);
	}

	FString MakeNoWaterActivityMessage(const FString& Label, const FString& GateDetail)
	{
		return FString::Printf(TEXT("%s: GPU water outputs show no water activity (A0 %s)"), *Label, *GateDetail);
	}

	FString MakeTerrainTimeoutMessage(const FString& Label, int32 TickLimit)
	{
		return FString::Printf(TEXT("%s: terrain sampling did not complete within %d ticks"), *Label, TickLimit);
	}

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

		void SetValue(int32 NewValue)
		{
			if (Variable)
			{
				Variable->Set(NewValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		FString PreviousValue;
	};

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	bool CanRunWaterRhiTest(FAutomationTestBase& Test, const FString& CaseLabel)
	{
		const bool bHasActiveRenderingRhi = HasRenderingRhi();
		UE_LOG(LogTDFluidGpuWaterRhiTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), *CaseLabel, bHasActiveRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (bHasActiveRenderingRhi)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), *CaseLabel));
		return false;
	}

	bool ExpectWaterGpuRegistered(FAutomationTestBase& Test, const FString& CaseLabel, const FTDWaterGpuConsoleVariableScope& WaterGpuScope)
	{
		if (WaterGpuScope.IsValid())
		{
			return true;
		}

		Test.AddError(MakeMissingWaterGpuMessage(CaseLabel));
		return false;
	}

	void ReportLine(FAutomationTestBase& Test, const FString& Line)
	{
		UE_LOG(LogTDFluidGpuWaterRhiTests, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
	}

	struct FTDFluidGpuWaterRhiTestWorld
	{
		FTDFluidGpuWaterRhiTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidGpuWaterRhiWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidGpuWaterRhiTestWorld()
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

		FTDFluidGpuWaterRhiTestWorld(const FTDFluidGpuWaterRhiTestWorld&) = delete;
		FTDFluidGpuWaterRhiTestWorld& operator=(const FTDFluidGpuWaterRhiTestWorld&) = delete;

		UTDFluidLiveSubsystem* GetFluidSubsystem() const
		{
			return World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
		}

		void StepWorld(float DeltaSeconds)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
			ENQUEUE_RENDER_COMMAND(TDFluidGpuWaterRhiWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	FTDFluidSimulationConfig MakeWaterArmConfig(int32 Resolution, double ExtentCm)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = Resolution;
		Config.SimExtentCm = FVector2D(ExtentCm, ExtentCm);
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
		return Config;
	}

	FTDFluidSimulationConfig MakeNonWaterConfig(int32 Resolution)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.SolverBackend = ETDFluidSolverBackend::Gpu;
		Config.GridResolution = Resolution;
		Config.SimExtentCm = FVector2D(2000.0, 2000.0);
		Config.VelocityDamping = 0.08f;
		Config.DensityDissipation = 0.2f;
		Config.VorticityStrength = 1.2f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 70.0f;
		Config.FlowNoiseScaleCm = 220.0f;
		Config.BuoyancyCmPerSec2 = 150.0f;
		Config.MaxPointsPerTick = 64;
		Config.bEnableWaterLayer = false;
		return Config;
	}

	FTDFluidBrushStamp MakeOrbitSmokeStamp(const FVector& CenterLocation, int32 StepIndex)
	{
		const double Angle = 0.17 * StepIndex;
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = CenterLocation + FVector(FMath::Cos(Angle) * 300.0, FMath::Sin(Angle) * 300.0, 0.0);
		Stamp.WorldVelocity = FVector(-FMath::Sin(Angle) * 300.0, FMath::Cos(Angle) * 300.0, 0.0);
		Stamp.RadiusCm = 90.0f;
		Stamp.DensityStrength = 0.9f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelB_Fire;
		return Stamp;
	}

	TArray<FTDFluidBrushStamp> MakeOrbitWaterStamps(const FVector2D& OrbitCenter, int32 StepIndex)
	{
		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 StampIndex = 0; StampIndex < 2; ++StampIndex)
		{
			const double Angle = StepIndex * 0.09 + StampIndex * UE_DOUBLE_PI;
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector(OrbitCenter.X + FMath::Cos(Angle) * 300.0, OrbitCenter.Y + FMath::Sin(Angle) * 90.0, 0.0);
			Stamp.WorldVelocity = FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0) * 400.0;
			Stamp.RadiusCm = 70.0f;
			Stamp.DensityStrength = 0.6f;
			Stamp.VelocityStrength = 1.0f;
			Stamp.RippleImpulseCm = 2.5f;
			Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
			Stamps.Add(Stamp);
		}
		return Stamps;
	}

	struct FTDFluidWaterRiverSpec
	{
		TArray<FVector> Points;
		TArray<float> WidthScales;
		float WidthCm = 800.0f;
		float SpeedCmPerSec = 200.0f;
	};

	struct FTDFluidWaterBlockSpec
	{
		FVector Center = FVector::ZeroVector;
		FVector Scale = FVector::OneVector;
		FRotator Rotation = FRotator::ZeroRotator;
	};

	struct FTDFluidWaterRockSpec
	{
		FVector2D Center = FVector2D::ZeroVector;
		float RadiusCm = 70.0f;
	};

	struct FTDFluidWaterSceneSpec
	{
		FTDFluidSimulationConfig Config;
		TArray<FTDFluidWaterRiverSpec> Rivers;
		TArray<FTDFluidWaterBlockSpec> Blocks;
		TArray<FTDFluidWaterRegion> Regions;
		TArray<FTDFluidWaterRockSpec> Rocks;
		FVector AnchorLocation = FVector::ZeroVector;
		float StepSeconds = StepSeconds60;
		bool bSamplesTerrain = true;
		bool bWaitsForTerrain = true;
	};

	FTDFluidWaterRiverSpec MakeStraightRiver(double StartX, double EndX, float HalfWidthCm, float SpeedCmPerSec)
	{
		FTDFluidWaterRiverSpec River;
		River.Points = { FVector(StartX, 0.0, WaterSurfaceZCm), FVector(EndX, 0.0, WaterSurfaceZCm) };
		River.WidthScales = { 1.0f, 1.0f };
		River.WidthCm = HalfWidthCm * 2.0f;
		River.SpeedCmPerSec = SpeedCmPerSec;
		return River;
	}

	FTDFluidWaterRiverSpec MakeProfiledRiver(const TArray<double>& PointsX, const TArray<float>& WidthScales, float HalfWidthCm, float SpeedCmPerSec)
	{
		FTDFluidWaterRiverSpec River;
		for (const double PointX : PointsX)
		{
			River.Points.Add(FVector(PointX, 0.0, WaterSurfaceZCm));
		}
		River.WidthScales = WidthScales;
		River.WidthCm = HalfWidthCm * 2.0f;
		River.SpeedCmPerSec = SpeedCmPerSec;
		return River;
	}

	FTDFluidWaterBlockSpec MakeFloorBlock()
	{
		FTDFluidWaterBlockSpec Block;
		Block.Center = FVector(0.0, 0.0, FloorTopZCm - CubeHalfSizeCm);
		Block.Scale = FVector(100.0, 100.0, 1.0);
		return Block;
	}

	FTDFluidWaterRegion MakeRegion(const FVector2D& MinXY, const FVector2D& MaxXY, float SurfaceHeightCm)
	{
		FTDFluidWaterRegion Region;
		Region.MinXY = MinXY;
		Region.MaxXY = MaxXY;
		Region.SurfaceHeightCm = SurfaceHeightCm;
		return Region;
	}

	FTDFluidWaterSceneSpec MakeRiverScene(int32 Resolution, double ExtentCm, float SceneStepSeconds, const FTDFluidWaterRiverSpec& River)
	{
		FTDFluidWaterSceneSpec SceneSpec;
		SceneSpec.Config = MakeWaterArmConfig(Resolution, ExtentCm);
		SceneSpec.Rivers.Add(River);
		SceneSpec.Blocks.Add(MakeFloorBlock());
		SceneSpec.StepSeconds = SceneStepSeconds;
		return SceneSpec;
	}

	struct FTDFluidWaterSceneAssets
	{
		UStaticMesh* CubeMesh = nullptr;
		UMaterialInterface* ShapeMaterial = nullptr;

		bool IsValid() const
		{
			return CubeMesh && ShapeMaterial;
		}
	};

	FTDFluidWaterSceneAssets LoadSceneAssets()
	{
		FTDFluidWaterSceneAssets Assets;
		Assets.CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
		Assets.ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath);
		return Assets;
	}

	AActor* SpawnSceneAnchor(UWorld& SceneWorld, const FVector& Location)
	{
		AActor* Anchor = SceneWorld.SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!Anchor)
		{
			return nullptr;
		}

		USceneComponent* AnchorRoot = NewObject<USceneComponent>(Anchor);
		Anchor->AddInstanceComponent(AnchorRoot);
		Anchor->SetRootComponent(AnchorRoot);
		AnchorRoot->RegisterComponent();
		Anchor->SetActorLocation(Location);
		return Anchor;
	}

	AActor* SpawnColliderBlock(UWorld& SceneWorld, UStaticMesh& CubeMesh, const FTDFluidWaterBlockSpec& Block)
	{
		AActor* BlockActor = SceneWorld.SpawnActor<AActor>();
		if (!BlockActor)
		{
			return nullptr;
		}

		UStaticMeshComponent* BlockMesh = NewObject<UStaticMeshComponent>(BlockActor);
		BlockActor->AddInstanceComponent(BlockMesh);
		BlockActor->SetRootComponent(BlockMesh);
		BlockMesh->SetMobility(EComponentMobility::Movable);
		BlockMesh->SetStaticMesh(&CubeMesh);
		BlockMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		BlockMesh->SetCollisionObjectType(ECC_WorldStatic);
		BlockMesh->SetCollisionResponseToAllChannels(ECR_Block);
		BlockMesh->SetWorldTransform(FTransform(Block.Rotation, Block.Center, Block.Scale));
		BlockMesh->RegisterComponent();
		return BlockActor;
	}

	AActor* SpawnTaggedBox(UWorld& SceneWorld, const FVector& Center, const FVector& HalfExtents, FName Tag)
	{
		AActor* BoxActor = SceneWorld.SpawnActor<AActor>();
		if (!BoxActor)
		{
			return nullptr;
		}

		UBoxComponent* Box = NewObject<UBoxComponent>(BoxActor);
		BoxActor->AddInstanceComponent(Box);
		BoxActor->SetRootComponent(Box);
		Box->SetBoxExtent(HalfExtents);
		Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Box->RegisterComponent();
		BoxActor->SetActorLocation(Center);
		BoxActor->Tags.Add(Tag);
		return BoxActor;
	}

	AActor* SpawnRegionBox(UWorld& SceneWorld, const FTDFluidWaterRegion& Region)
	{
		constexpr double RegionHalfHeightCm = 50.0;
		const FVector2D CenterXY = 0.5 * (Region.MinXY + Region.MaxXY);
		const FVector2D HalfXY = 0.5 * (Region.MaxXY - Region.MinXY);
		return SpawnTaggedBox(SceneWorld, FVector(CenterXY.X, CenterXY.Y, Region.SurfaceHeightCm - RegionHalfHeightCm), FVector(HalfXY.X, HalfXY.Y, RegionHalfHeightCm), GetWaterRegionTag());
	}

	AActor* SpawnRockMarker(UWorld& SceneWorld, const FTDFluidWaterRockSpec& Rock)
	{
		return SpawnTaggedBox(SceneWorld, FVector(Rock.Center.X, Rock.Center.Y, 0.0), FVector(Rock.RadiusCm, Rock.RadiusCm, 100.0), GetWaterObstacleTag());
	}

	UStaticMeshComponent* SpawnSurfaceMesh(UWorld& SceneWorld, UStaticMesh& CubeMesh, UMaterialInterface& ShapeMaterial)
	{
		AActor* SurfaceActor = SceneWorld.SpawnActor<AActor>();
		if (!SurfaceActor)
		{
			return nullptr;
		}

		UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(SurfaceActor);
		SurfaceActor->AddInstanceComponent(MeshComponent);
		SurfaceActor->SetRootComponent(MeshComponent);
		MeshComponent->SetMobility(EComponentMobility::Movable);
		MeshComponent->SetStaticMesh(&CubeMesh);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetMaterial(0, &ShapeMaterial);
		MeshComponent->RegisterComponent();
		SurfaceActor->SetActorLocation(FVector(0.0, 0.0, 400.0));
		SurfaceActor->Tags.Add(GetWaterSurfaceTag());
		return MeshComponent;
	}

	ATDFluidRiverActor* SpawnRiverActor(UWorld& SceneWorld, const FTDFluidWaterRiverSpec& RiverSpec)
	{
		ATDFluidRiverActor* River = SceneWorld.SpawnActorDeferred<ATDFluidRiverActor>(ATDFluidRiverActor::StaticClass(), FTransform::Identity);
		if (!River || !River->RiverSpline)
		{
			return nullptr;
		}

		River->RiverWidthCm = RiverSpec.WidthCm;
		River->FlowSpeedCmPerSec = RiverSpec.SpeedCmPerSec;
		River->GuideSampleSpacingCm = 50.0f;
		River->bBuildRibbonSurface = false;

		USplineComponent* Spline = River->RiverSpline;
		Spline->ClearSplinePoints(false);
		for (int32 PointIndex = 0; PointIndex < RiverSpec.Points.Num(); ++PointIndex)
		{
			Spline->AddSplinePoint(RiverSpec.Points[PointIndex], ESplineCoordinateSpace::World, false);
			Spline->SetSplinePointType(PointIndex, ESplinePointType::Linear, false);
			if (RiverSpec.WidthScales.IsValidIndex(PointIndex))
			{
				Spline->SetScaleAtSplinePoint(PointIndex, FVector(1.0, RiverSpec.WidthScales[PointIndex], 1.0), false);
			}
		}
		Spline->UpdateSpline();
		River->FinishSpawning(FTransform::Identity);
		return River;
	}

	ATDFluidWaterActor* SpawnSceneWaterActor(UWorld& SceneWorld, AActor& Anchor, const FTDFluidWaterSceneSpec& SceneSpec, ETDFluidSolverBackend Backend)
	{
		ATDFluidWaterActor* WaterActor = SceneWorld.SpawnActor<ATDFluidWaterActor>(ATDFluidWaterActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (!WaterActor || !WaterActor->LiveComponent)
		{
			return nullptr;
		}

		WaterActor->FollowTargetActor = &Anchor;
		WaterActor->ViewLookAheadCm = 0.0f;
		WaterActor->bSleepWhenNoWaterInWindow = false;
		WaterActor->bSampleTerrainElevation = SceneSpec.bSamplesTerrain;
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(&SceneWorld, ETDFluidPresetKind::River);
		Preset->bOverridePresetDefaults = true;
		Preset->SimulationConfig = SceneSpec.Config;
		Preset->SimulationConfig.SolverBackend = Backend;
		WaterActor->InitializeFromPreset(Preset, 0.0f);
		return WaterActor;
	}

	ATDFluidLiveActor* SpawnConfiguredLiveActor(UWorld& SceneWorld, const FVector& Location, const FTDFluidSimulationConfig& Config)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
		ATDFluidLiveActor* LiveActor = SceneWorld.SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!LiveActor || !LiveActor->LiveComponent)
		{
			return nullptr;
		}

		LiveActor->LiveComponent->SimulationConfig = Config;
		LiveActor->FinishSpawning(SpawnTransform);
		return LiveActor;
	}

	UTexture* ResolveSurfaceTexture(UMaterialInstanceDynamic* SurfaceMaterial)
	{
		return SurfaceMaterial ? SurfaceMaterial->K2_GetTextureParameterValue(TEXT("WaterSurfaceTexture")) : nullptr;
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

	uint32 ComputePixelChecksum(const TArray<FFloat16Color>& Pixels)
	{
		return FCrc::MemCrc32(Pixels.GetData(), Pixels.Num() * sizeof(FFloat16Color));
	}

	bool ArePixelsBitIdentical(const TArray<FFloat16Color>& Expected, const TArray<FFloat16Color>& Actual)
	{
		return Expected.Num() == Actual.Num() && FMemory::Memcmp(Expected.GetData(), Actual.GetData(), Expected.Num() * sizeof(FFloat16Color)) == 0;
	}

	struct FTDFluidWaterArmOutputs
	{
		int32 Resolution = 0;
		FVector2D CenterWorld = FVector2D::ZeroVector;
		FVector2D ExtentCm = FVector2D::ZeroVector;
		bool bUsesGpuSolver = false;
		TArray<FFloat16Color> DensityPixels;
		TArray<FFloat16Color> VelocityPixels;
		TArray<FFloat16Color> SurfacePixels;

		int32 GetCellCount() const
		{
			return Resolution * Resolution;
		}

		bool HasAllPixels() const
		{
			const int32 CellCount = GetCellCount();
			return CellCount > 0 && DensityPixels.Num() == CellCount && VelocityPixels.Num() == CellCount && SurfacePixels.Num() == CellCount;
		}

		FVector2D GetCellSizeCm() const
		{
			return ExtentCm / static_cast<double>(FMath::Max(Resolution, 1));
		}

		FVector2D GetMinimumCorner() const
		{
			return CenterWorld - 0.5 * ExtentCm;
		}

		FVector2D GetCellCenterWorld(int32 CellX, int32 CellY) const
		{
			const FVector2D CellSize = GetCellSizeCm();
			return GetMinimumCorner() + FVector2D((CellX + 0.5) * CellSize.X, (CellY + 0.5) * CellSize.Y);
		}

		int32 FindColumn(double WorldX) const
		{
			const int32 CellX = FMath::FloorToInt32((WorldX - GetMinimumCorner().X) / GetCellSizeCm().X);
			return CellX >= 0 && CellX < Resolution ? CellX : INDEX_NONE;
		}

		int32 FindCellIndex(const FVector2D& WorldPoint) const
		{
			if (Resolution <= 0)
			{
				return INDEX_NONE;
			}

			const FVector2D CellSize = GetCellSizeCm();
			const int32 CellX = FMath::FloorToInt32((WorldPoint.X - GetMinimumCorner().X) / CellSize.X);
			const int32 CellY = FMath::FloorToInt32((WorldPoint.Y - GetMinimumCorner().Y) / CellSize.Y);
			if (CellX < 0 || CellX >= Resolution || CellY < 0 || CellY >= Resolution)
			{
				return INDEX_NONE;
			}
			return CellY * Resolution + CellX;
		}

		FVector2D DecodeVelocity(int32 CellIndex) const
		{
			const double SpeedReference = static_cast<double>(FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec());
			const FFloat16Color& Pixel = VelocityPixels[CellIndex];
			return FVector2D((Pixel.R.GetFloat() * 2.0 - 1.0) * SpeedReference, (Pixel.G.GetFloat() * 2.0 - 1.0) * SpeedReference);
		}

		bool IsSolid(int32 CellIndex) const
		{
			return DensityPixels[CellIndex].A.GetFloat() <= 0.0f;
		}

		float GetDensity(int32 CellIndex) const
		{
			return DensityPixels[CellIndex].R.GetFloat();
		}

		float GetDepthEncoding(int32 CellIndex) const
		{
			return SurfacePixels[CellIndex].R.GetFloat();
		}

		float DecodeRippleHeightCm(int32 CellIndex, float RippleReferenceCm) const
		{
			return (SurfacePixels[CellIndex].B.GetFloat() - 0.5f) * 2.0f * RippleReferenceCm;
		}

		bool IsTerrainUnknown(int32 CellIndex) const
		{
			return FMath::IsNearlyEqual(SurfacePixels[CellIndex].A.GetFloat(), UnknownTerrainEncoding, EncodingTolerance);
		}
	};

	FTDFluidWaterArmOutputs CaptureArmOutputs(const UTDFluidLiveComponent& Component, UMaterialInstanceDynamic* SurfaceMaterial)
	{
		FTDFluidWaterArmOutputs Outputs;
		Outputs.Resolution = Component.GetSimulationResolution();
		Outputs.CenterWorld = Component.GetSimulationCenterWorld();
		Outputs.ExtentCm = Component.GetSimulationExtentCm();
		Outputs.bUsesGpuSolver = Component.IsUsingGpuSolver();
		if (!Outputs.bUsesGpuSolver)
		{
			Outputs.DensityPixels = Component.GetPackedDensityPressurePixels();
			Outputs.VelocityPixels = Component.GetPackedVelocityPixels();
			Outputs.SurfacePixels = Component.GetPackedWaterSurfacePixels();
			return Outputs;
		}

		ReadRenderTargetPixels(Component.GetDensityPressureTexture(), Outputs.DensityPixels);
		ReadRenderTargetPixels(Component.GetVelocityTexture(), Outputs.VelocityPixels);
		ReadRenderTargetPixels(ResolveSurfaceTexture(SurfaceMaterial), Outputs.SurfacePixels);
		return Outputs;
	}

	struct FTDFluidWaterTargetInfo
	{
		bool bHasDensityTarget = false;
		bool bHasVelocityTarget = false;
		bool bHasSurfaceTarget = false;
		FIntPoint DensitySize = FIntPoint::ZeroValue;
		FIntPoint VelocitySize = FIntPoint::ZeroValue;
		FIntPoint SurfaceSize = FIntPoint::ZeroValue;
		EPixelFormat DensityFormat = PF_Unknown;
		EPixelFormat VelocityFormat = PF_Unknown;
		EPixelFormat SurfaceFormat = PF_Unknown;
	};

	FTDFluidWaterTargetInfo DescribeWaterTargets(const UTDFluidLiveComponent& Component, UMaterialInstanceDynamic* SurfaceMaterial)
	{
		FTDFluidWaterTargetInfo Info;
		if (const UTextureRenderTarget2D* DensityTarget = Cast<UTextureRenderTarget2D>(Component.GetDensityPressureTexture()))
		{
			Info.bHasDensityTarget = true;
			Info.DensitySize = FIntPoint(DensityTarget->SizeX, DensityTarget->SizeY);
			Info.DensityFormat = DensityTarget->GetFormat();
		}
		if (const UTextureRenderTarget2D* VelocityTarget = Cast<UTextureRenderTarget2D>(Component.GetVelocityTexture()))
		{
			Info.bHasVelocityTarget = true;
			Info.VelocitySize = FIntPoint(VelocityTarget->SizeX, VelocityTarget->SizeY);
			Info.VelocityFormat = VelocityTarget->GetFormat();
		}
		if (const UTextureRenderTarget2D* SurfaceTarget = Cast<UTextureRenderTarget2D>(ResolveSurfaceTexture(SurfaceMaterial)))
		{
			Info.bHasSurfaceTarget = true;
			Info.SurfaceSize = FIntPoint(SurfaceTarget->SizeX, SurfaceTarget->SizeY);
			Info.SurfaceFormat = SurfaceTarget->GetFormat();
		}
		return Info;
	}

	bool ExpectWaterTargets(FAutomationTestBase& Test, const FString& ArmLabel, const FTDFluidWaterTargetInfo& Info, int32 ExpectedResolution)
	{
		const FIntPoint ExpectedSize(ExpectedResolution, ExpectedResolution);
		const bool bHasDensity = Test.TestTrue(*(ArmLabel + TEXT(" density-pressure output is a half float RGBA render target at the water tier")), Info.bHasDensityTarget && Info.DensitySize == ExpectedSize && Info.DensityFormat == PF_FloatRGBA);
		const bool bHasVelocity = Test.TestTrue(*(ArmLabel + TEXT(" velocity output is a half float RGBA render target at the water tier")), Info.bHasVelocityTarget && Info.VelocitySize == ExpectedSize && Info.VelocityFormat == PF_FloatRGBA);
		const bool bHasSurface = Test.TestTrue(*(ArmLabel + TEXT(" shared material WaterSurfaceTexture is a half float RGBA render target at the water tier")), Info.bHasSurfaceTarget && Info.SurfaceSize == ExpectedSize && Info.SurfaceFormat == PF_FloatRGBA);
		return bHasDensity && bHasVelocity && bHasSurface;
	}

	bool ExpectGpuArmOutputs(FAutomationTestBase& Test, const FString& ArmLabel, const FTDFluidWaterTargetInfo& Info, const FTDFluidWaterArmOutputs& Outputs, int32 ExpectedResolution)
	{
		if (!ExpectWaterTargets(Test, ArmLabel, Info, ExpectedResolution))
		{
			return false;
		}
		return Test.TestTrue(*(ArmLabel + TEXT(" read every GPU output pixel")), Outputs.HasAllPixels());
	}

	struct FTDFluidWaterGuideOracle
	{
		int32 Resolution = 0;
		FVector2D CenterWorld = FVector2D::ZeroVector;
		FVector2D ExtentCm = FVector2D::ZeroVector;
		TArray<float> Weights;
		TArray<FVector2D> Targets;

		bool IsValid() const
		{
			return Resolution > 0 && Weights.Num() == Resolution * Resolution && Targets.Num() == Weights.Num();
		}

		int32 FindCellIndex(const FVector2D& WorldPoint) const
		{
			if (!IsValid())
			{
				return INDEX_NONE;
			}

			const FVector2D MinimumCorner = CenterWorld - 0.5 * ExtentCm;
			const int32 CellX = FMath::FloorToInt32((WorldPoint.X - MinimumCorner.X) / ExtentCm.X * Resolution);
			const int32 CellY = FMath::FloorToInt32((WorldPoint.Y - MinimumCorner.Y) / ExtentCm.Y * Resolution);
			if (CellX < 0 || CellX >= Resolution || CellY < 0 || CellY >= Resolution)
			{
				return INDEX_NONE;
			}
			return CellY * Resolution + CellX;
		}

		double MeasureTargetFlux(double WorldX) const
		{
			if (!IsValid())
			{
				return 0.0;
			}

			const FVector2D MinimumCorner = CenterWorld - 0.5 * ExtentCm;
			const int32 CellX = FMath::FloorToInt32((WorldX - MinimumCorner.X) / ExtentCm.X * Resolution);
			if (CellX < 0 || CellX >= Resolution)
			{
				return 0.0;
			}

			const double CellSizeY = ExtentCm.Y / Resolution;
			double TargetFlux = 0.0;
			for (int32 CellY = 0; CellY < Resolution; ++CellY)
			{
				const int32 CellIndex = CellY * Resolution + CellX;
				TargetFlux += Targets[CellIndex].X * Weights[CellIndex] * CellSizeY;
			}
			return TargetFlux;
		}
	};

	FTDFluidWaterGuideOracle BuildGuideOracle(const FTDFluidSimulationConfig& ArmConfig, const FVector2D& CenterWorld, const FVector2D& ExtentCm, const TArray<FTDFluidFlowGuide>& Guides, const TArray<FTDFluidWaterRegion>& Regions)
	{
		FTDFluidSimulationConfig OracleConfig = ArmConfig;
		OracleConfig.SolverBackend = ETDFluidSolverBackend::Cpu;
		OracleConfig.GridResolution = OracleResolution;
		OracleConfig.SimExtentCm = ExtentCm;
		OracleConfig.bEnableWaterLayer = true;

		FTDFluidGrid2DSolver OracleSolver;
		OracleSolver.Initialize(OracleConfig, CenterWorld);
		OracleSolver.SetFlowGuides(Guides);
		OracleSolver.SetWaterRegions(Regions);
		OracleSolver.StepSimulation(StepSeconds60);

		FTDFluidWaterGuideOracle Oracle;
		Oracle.Resolution = OracleSolver.GetResolution();
		Oracle.CenterWorld = OracleSolver.GetCenterWorld();
		Oracle.ExtentCm = OracleSolver.GetExtentCm();
		Oracle.Weights.SetNumZeroed(Oracle.Resolution * Oracle.Resolution);
		Oracle.Targets.SetNumZeroed(Oracle.Resolution * Oracle.Resolution);
		for (int32 CellY = 0; CellY < Oracle.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Oracle.Resolution; ++CellX)
			{
				const FTDFluidCellSample Sample = OracleSolver.SampleAtCell(CellX, CellY);
				Oracle.Weights[CellY * Oracle.Resolution + CellX] = Sample.FlowGuideWeight;
				Oracle.Targets[CellY * Oracle.Resolution + CellX] = Sample.FlowTargetVelocity;
			}
		}
		return Oracle;
	}

	struct FTDFluidWaterArmContext
	{
		FTDFluidGpuWaterRhiTestWorld* ArmWorld = nullptr;
		const FTDFluidWaterSceneSpec* Scene = nullptr;
		FTDFluidWaterSceneAssets Assets;
		ATDFluidWaterActor* WaterActor = nullptr;
		AActor* Anchor = nullptr;
		UStaticMeshComponent* SurfaceMesh = nullptr;
		TArray<FTDFluidFlowGuide> Guides;

		UTDFluidLiveComponent& GetComponent() const
		{
			return *WaterActor->LiveComponent;
		}

		UMaterialInstanceDynamic* GetSurfaceMaterial() const
		{
			return SurfaceMesh ? Cast<UMaterialInstanceDynamic>(SurfaceMesh->GetMaterial(0)) : nullptr;
		}

		void Step() const
		{
			ArmWorld->StepWorld(Scene->StepSeconds);
		}

		void StepFor(float DeltaSeconds) const
		{
			ArmWorld->StepWorld(DeltaSeconds);
		}

		void QueueStamp(const FTDFluidBrushStamp& Stamp) const
		{
			GetComponent().QueueBrushStamp(Stamp);
		}

		FTDFluidWaterArmOutputs Capture() const
		{
			return CaptureArmOutputs(GetComponent(), GetSurfaceMaterial());
		}

		FTDFluidWaterTargetInfo DescribeTargets() const
		{
			return DescribeWaterTargets(GetComponent(), GetSurfaceMaterial());
		}

		FTDFluidWaterGuideOracle BuildOracle() const
		{
			return BuildGuideOracle(Scene->Config, GetComponent().GetSimulationCenterWorld(), GetComponent().GetSimulationExtentCm(), Guides, Scene->Regions);
		}

		bool AddRiver(const FTDFluidWaterRiverSpec& RiverSpec)
		{
			const ATDFluidRiverActor* River = SpawnRiverActor(*ArmWorld->World, RiverSpec);
			if (!River)
			{
				return false;
			}

			FTDFluidFlowGuide& Guide = Guides.AddDefaulted_GetRef();
			River->BuildFlowGuide(Guide);
			return true;
		}
	};

	bool WaitForTerrainSampling(FAutomationTestBase& Test, const FString& ArmLabel, const FTDFluidWaterArmContext& ArmContext, TFunctionRef<void(int32)> OnTerrainTick)
	{
		for (int32 TickIndex = 0; TickIndex < TerrainWaitTickLimit; ++TickIndex)
		{
			if (ArmContext.GetComponent().GetWaterLayerDisplayAlpha() > 0.0f)
			{
				return true;
			}

			ArmContext.Step();
			OnTerrainTick(TickIndex);
		}

		if (ArmContext.GetComponent().GetWaterLayerDisplayAlpha() > 0.0f)
		{
			return true;
		}

		Test.AddError(MakeTerrainTimeoutMessage(ArmLabel, TerrainWaitTickLimit));
		return false;
	}

	bool RunWaterActorArm(FAutomationTestBase& Test, const FString& ArmLabel, const FTDFluidWaterSceneSpec& SceneSpec, ETDFluidSolverBackend Backend, TFunctionRef<bool(FTDFluidWaterArmContext&)> RunScenario)
	{
		FTDFluidGpuWaterRhiTestWorld ArmTestWorld;
		FTDFluidWaterArmContext ArmContext;
		ArmContext.ArmWorld = &ArmTestWorld;
		ArmContext.Scene = &SceneSpec;
		ArmContext.Assets = LoadSceneAssets();
		if (!Test.TestTrue(*(ArmLabel + TEXT(" loaded the engine cube mesh and shape material")), ArmContext.Assets.IsValid()))
		{
			return false;
		}

		UWorld& SceneWorld = *ArmTestWorld.World;
		for (const FTDFluidWaterBlockSpec& Block : SceneSpec.Blocks)
		{
			SpawnColliderBlock(SceneWorld, *ArmContext.Assets.CubeMesh, Block);
		}
		for (const FTDFluidWaterRegion& Region : SceneSpec.Regions)
		{
			SpawnRegionBox(SceneWorld, Region);
		}
		for (const FTDFluidWaterRockSpec& Rock : SceneSpec.Rocks)
		{
			SpawnRockMarker(SceneWorld, Rock);
		}
		for (const FTDFluidWaterRiverSpec& RiverSpec : SceneSpec.Rivers)
		{
			if (!ArmContext.AddRiver(RiverSpec))
			{
				Test.AddError(FString::Printf(TEXT("%s: the river actor could not be spawned (harness defect)"), *ArmLabel));
				return false;
			}
		}

		ArmContext.SurfaceMesh = SpawnSurfaceMesh(SceneWorld, *ArmContext.Assets.CubeMesh, *ArmContext.Assets.ShapeMaterial);
		ArmContext.Anchor = SpawnSceneAnchor(SceneWorld, SceneSpec.AnchorLocation);
		ArmContext.WaterActor = ArmContext.Anchor ? SpawnSceneWaterActor(SceneWorld, *ArmContext.Anchor, SceneSpec, Backend) : nullptr;
		if (!Test.TestTrue(*(ArmLabel + TEXT(" spawned the water actor scene")), ArmContext.SurfaceMesh != nullptr && ArmContext.Anchor != nullptr && ArmContext.WaterActor != nullptr))
		{
			return false;
		}

		ArmContext.Step();
		if (Backend == ETDFluidSolverBackend::Gpu && !ArmContext.GetComponent().IsUsingGpuSolver())
		{
			Test.AddError(MakeGpuNotSelectedMessage(ArmLabel));
			return false;
		}
		if (SceneSpec.bWaitsForTerrain && !WaitForTerrainSampling(Test, ArmLabel, ArmContext, [](int32) {}))
		{
			return false;
		}
		return RunScenario(ArmContext);
	}

	struct FTDFluidWaterPairContext
	{
		FTDFluidGpuWaterRhiTestWorld* PairWorld = nullptr;
		ATDFluidLiveActor* CpuActor = nullptr;
		ATDFluidLiveActor* GpuActor = nullptr;
		TStrongObjectPtr<UMaterialInstanceDynamic> CpuSurfaceMaterial;
		TStrongObjectPtr<UMaterialInstanceDynamic> GpuSurfaceMaterial;

		void QueueStamp(const FTDFluidBrushStamp& Stamp) const
		{
			CpuActor->LiveComponent->QueueBrushStamp(Stamp);
			if (GpuActor)
			{
				GpuActor->LiveComponent->QueueBrushStamp(Stamp);
			}
		}

		void Step(float DeltaSeconds) const
		{
			PairWorld->StepWorld(DeltaSeconds);
		}

		FVector GetCellCenterWorld(int32 CellX, int32 CellY) const
		{
			const FVector2D PlanePoint = CpuActor->LiveComponent->CellToPlaneCenter(CellX, CellY);
			return FVector(PlanePoint.X, PlanePoint.Y, 0.0);
		}

		FTDFluidWaterArmOutputs CaptureCpu() const
		{
			return CaptureArmOutputs(*CpuActor->LiveComponent, CpuSurfaceMaterial.Get());
		}

		FTDFluidWaterArmOutputs CaptureGpu() const
		{
			return GpuActor ? CaptureArmOutputs(*GpuActor->LiveComponent, GpuSurfaceMaterial.Get()) : FTDFluidWaterArmOutputs();
		}

		FTDFluidWaterTargetInfo DescribeGpuTargets() const
		{
			return GpuActor ? DescribeWaterTargets(*GpuActor->LiveComponent, GpuSurfaceMaterial.Get()) : FTDFluidWaterTargetInfo();
		}
	};

	struct FTDFluidWaterPairResult
	{
		bool bHasGpuArm = false;
		bool bGpuArmUsesGpu = false;
	};

	UMaterialInstanceDynamic* RegisterSurfaceMaterial(ATDFluidLiveActor& LiveActor, UMaterialInterface& ShapeMaterial)
	{
		UMaterialInstanceDynamic* SurfaceMaterial = UMaterialInstanceDynamic::Create(&ShapeMaterial, &LiveActor);
		LiveActor.LiveComponent->RegisterExternalMaterialTarget(SurfaceMaterial);
		return SurfaceMaterial;
	}

	bool RunLiveActorPair(FAutomationTestBase& Test, const FString& PairLabel, const FTDFluidSimulationConfig& PairConfig, bool bSpawnsGpuArm, FTDFluidWaterPairResult& OutResult, TFunctionRef<bool(FTDFluidWaterPairContext&)> RunScenario)
	{
		FTDFluidGpuWaterRhiTestWorld PairTestWorld;
		UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath);
		if (!Test.TestNotNull(*(PairLabel + TEXT(" loaded the shape material")), ShapeMaterial))
		{
			return false;
		}

		FTDFluidWaterPairContext PairContext;
		PairContext.PairWorld = &PairTestWorld;
		FTDFluidSimulationConfig CpuConfig = PairConfig;
		CpuConfig.SolverBackend = ETDFluidSolverBackend::Cpu;
		PairContext.CpuActor = SpawnConfiguredLiveActor(*PairTestWorld.World, FVector::ZeroVector, CpuConfig);
		if (!Test.TestNotNull(*(PairLabel + TEXT(" spawned the CPU water live actor")), PairContext.CpuActor))
		{
			return false;
		}
		PairContext.CpuSurfaceMaterial.Reset(RegisterSurfaceMaterial(*PairContext.CpuActor, *ShapeMaterial));

		if (bSpawnsGpuArm)
		{
			FTDFluidSimulationConfig GpuConfig = PairConfig;
			GpuConfig.SolverBackend = ETDFluidSolverBackend::Gpu;
			PairContext.GpuActor = SpawnConfiguredLiveActor(*PairTestWorld.World, FVector::ZeroVector, GpuConfig);
			if (!Test.TestNotNull(*(PairLabel + TEXT(" spawned the GPU water live actor")), PairContext.GpuActor))
			{
				return false;
			}
			PairContext.GpuSurfaceMaterial.Reset(RegisterSurfaceMaterial(*PairContext.GpuActor, *ShapeMaterial));
		}

		OutResult.bHasGpuArm = PairContext.GpuActor != nullptr;
		const bool bScenarioSucceeded = RunScenario(PairContext);
		OutResult.bGpuArmUsesGpu = PairContext.GpuActor && PairContext.GpuActor->LiveComponent->IsUsingGpuSolver();
		return bScenarioSucceeded;
	}

	bool ExpectPairGpuArm(FAutomationTestBase& Test, const FString& CaseLabel, const FTDWaterGpuConsoleVariableScope& WaterGpuScope, const FTDFluidWaterPairResult& PairResult)
	{
		if (!ExpectWaterGpuRegistered(Test, CaseLabel, WaterGpuScope))
		{
			return false;
		}
		if (PairResult.bHasGpuArm && PairResult.bGpuArmUsesGpu)
		{
			return true;
		}

		Test.AddError(MakeGpuNotSelectedMessage(CaseLabel));
		return false;
	}

	bool ExpectFlowActivity(FAutomationTestBase& Test, const FString& ArmLabel, const FTDFluidWaterArmOutputs& Outputs, const FTDFluidWaterGuideOracle& Oracle)
	{
		int32 GuideCellCount = 0;
		int32 DeepGuideCellCount = 0;
		int32 ChangedGuideCellCount = 0;
		double SpeedSum = 0.0;
		double TargetSpeedSum = 0.0;
		for (int32 CellY = 0; CellY < Outputs.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Outputs.Resolution; ++CellX)
			{
				const int32 OracleIndex = Oracle.FindCellIndex(Outputs.GetCellCenterWorld(CellX, CellY));
				if (OracleIndex == INDEX_NONE || Oracle.Weights[OracleIndex] < GuideWeightThreshold)
				{
					continue;
				}

				const int32 CellIndex = CellY * Outputs.Resolution + CellX;
				const FFloat16Color& VelocityPixel = Outputs.VelocityPixels[CellIndex];
				const bool bMatchesClearColor = FMath::IsNearlyEqual(VelocityPixel.R.GetFloat(), 0.5f, 1.0e-4f)
					&& FMath::IsNearlyEqual(VelocityPixel.G.GetFloat(), 0.5f, 1.0e-4f)
					&& FMath::IsNearlyEqual(VelocityPixel.B.GetFloat(), 0.0f, 1.0e-4f)
					&& FMath::IsNearlyEqual(VelocityPixel.A.GetFloat(), 0.0f, 1.0e-4f);
				++GuideCellCount;
				DeepGuideCellCount += Outputs.GetDepthEncoding(CellIndex) > 0.0f ? 1 : 0;
				ChangedGuideCellCount += bMatchesClearColor ? 0 : 1;
				SpeedSum += Outputs.DecodeVelocity(CellIndex).Size();
				TargetSpeedSum += Oracle.Targets[OracleIndex].Size();
			}
		}

		const double DeepFraction = GuideCellCount > 0 ? static_cast<double>(DeepGuideCellCount) / GuideCellCount : 0.0;
		const double ChangedFraction = GuideCellCount > 0 ? static_cast<double>(ChangedGuideCellCount) / GuideCellCount : 0.0;
		const double SpeedRatio = TargetSpeedSum > 0.0 ? SpeedSum / TargetSpeedSum : 0.0;
		const FString GateDetail = FString::Printf(TEXT("flow guide_cells=%d depth_fraction=%.3f speed_ratio=%.3f changed_fraction=%.3f"), GuideCellCount, DeepFraction, SpeedRatio, ChangedFraction);
		ReportLine(Test, FString::Printf(TEXT("TDFluidGpuWaterActivity: %s %s"), *ArmLabel, *GateDetail));
		const bool bIsActive = GuideCellCount > 0 && DeepFraction >= 0.9 && SpeedRatio >= 0.5 && ChangedFraction >= 0.5;
		if (!bIsActive)
		{
			Test.AddError(MakeNoWaterActivityMessage(ArmLabel, GateDetail));
		}
		return bIsActive;
	}

	bool ExpectRippleActivity(FAutomationTestBase& Test, const FString& ArmLabel, const FTDFluidWaterArmOutputs& Outputs, float ImpulseCm, float RippleReferenceCm)
	{
		double MaximumHeightCm = 0.0;
		for (int32 CellIndex = 0; CellIndex < Outputs.SurfacePixels.Num(); ++CellIndex)
		{
			MaximumHeightCm = FMath::Max(MaximumHeightCm, static_cast<double>(FMath::Abs(Outputs.DecodeRippleHeightCm(CellIndex, RippleReferenceCm))));
		}

		const FString GateDetail = FString::Printf(TEXT("ripple max_height_cm=%.4f impulse_cm=%.3f"), MaximumHeightCm, ImpulseCm);
		ReportLine(Test, FString::Printf(TEXT("TDFluidGpuWaterActivity: %s %s"), *ArmLabel, *GateDetail));
		const bool bIsActive = MaximumHeightCm >= 0.5 * FMath::Abs(ImpulseCm);
		if (!bIsActive)
		{
			Test.AddError(MakeNoWaterActivityMessage(ArmLabel, GateDetail));
		}
		return bIsActive;
	}

	TArray<bool> MakeWetCellMask(const FTDFluidWaterArmOutputs& Outputs)
	{
		TArray<bool> WetMask;
		WetMask.Init(false, Outputs.GetCellCount());
		for (int32 CellIndex = 0; CellIndex < Outputs.SurfacePixels.Num() && CellIndex < WetMask.Num(); ++CellIndex)
		{
			WetMask[CellIndex] = Outputs.GetDepthEncoding(CellIndex) > 0.0f;
		}
		return WetMask;
	}

	bool ExpectStillActivity(FAutomationTestBase& Test, const FString& ArmLabel, const FTDFluidWaterArmOutputs& Outputs, const TArray<bool>& SubmergedMask)
	{
		int32 SubmergedCellCount = 0;
		int32 DeepSubmergedCellCount = 0;
		int32 KnownTerrainCellCount = 0;
		for (int32 CellIndex = 0; CellIndex < Outputs.GetCellCount(); ++CellIndex)
		{
			KnownTerrainCellCount += Outputs.IsTerrainUnknown(CellIndex) ? 0 : 1;
			if (!SubmergedMask.IsValidIndex(CellIndex) || !SubmergedMask[CellIndex])
			{
				continue;
			}
			++SubmergedCellCount;
			DeepSubmergedCellCount += Outputs.GetDepthEncoding(CellIndex) > 0.0f ? 1 : 0;
		}

		const double DeepFraction = SubmergedCellCount > 0 ? static_cast<double>(DeepSubmergedCellCount) / SubmergedCellCount : 0.0;
		const double KnownFraction = Outputs.GetCellCount() > 0 ? static_cast<double>(KnownTerrainCellCount) / Outputs.GetCellCount() : 0.0;
		const FString GateDetail = FString::Printf(TEXT("still submerged_cells=%d depth_fraction=%.3f terrain_known_fraction=%.3f"), SubmergedCellCount, DeepFraction, KnownFraction);
		ReportLine(Test, FString::Printf(TEXT("TDFluidGpuWaterActivity: %s %s"), *ArmLabel, *GateDetail));
		const bool bIsActive = SubmergedCellCount > 0 && DeepFraction >= 0.9 && KnownFraction >= 0.9;
		if (!bIsActive)
		{
			Test.AddError(MakeNoWaterActivityMessage(ArmLabel, GateDetail));
		}
		return bIsActive;
	}

	struct FTDFluidWaterOutputHealth
	{
		int32 NonFiniteChannelCount = 0;
		int32 OutOfRangeChannelCount = 0;
		double MaximumSpeedCmPerSec = 0.0;
	};

	FTDFluidWaterOutputHealth MeasureOutputHealth(const FTDFluidWaterArmOutputs& Outputs)
	{
		FTDFluidWaterOutputHealth Health;
		const auto CheckUnitChannel = [&Health](float ChannelValue)
		{
			if (!FMath::IsFinite(ChannelValue))
			{
				++Health.NonFiniteChannelCount;
				return;
			}
			Health.OutOfRangeChannelCount += (ChannelValue < -UnitRangeSlack || ChannelValue > 1.0f + UnitRangeSlack) ? 1 : 0;
		};

		for (int32 CellIndex = 0; CellIndex < Outputs.GetCellCount() && Outputs.HasAllPixels(); ++CellIndex)
		{
			for (const FFloat16Color* Pixel : { &Outputs.DensityPixels[CellIndex], &Outputs.VelocityPixels[CellIndex] })
			{
				CheckUnitChannel(Pixel->R.GetFloat());
				CheckUnitChannel(Pixel->G.GetFloat());
				CheckUnitChannel(Pixel->B.GetFloat());
				CheckUnitChannel(Pixel->A.GetFloat());
			}

			const FFloat16Color& SurfacePixel = Outputs.SurfacePixels[CellIndex];
			CheckUnitChannel(SurfacePixel.R.GetFloat());
			CheckUnitChannel(SurfacePixel.G.GetFloat());
			CheckUnitChannel(SurfacePixel.B.GetFloat());
			const float TerrainEncoding = SurfacePixel.A.GetFloat();
			if (!FMath::IsFinite(TerrainEncoding))
			{
				++Health.NonFiniteChannelCount;
			}
			else
			{
				Health.OutOfRangeChannelCount += FMath::Abs(TerrainEncoding) > UnknownTerrainEncoding + UnitRangeSlack ? 1 : 0;
			}
			Health.MaximumSpeedCmPerSec = FMath::Max(Health.MaximumSpeedCmPerSec, Outputs.DecodeVelocity(CellIndex).Size());
		}
		return Health;
	}

	struct FTDFluidWaterSectionFlux
	{
		double NetFlux = 0.0;
		double ReverseFlux = 0.0;
		double TargetFlux = 0.0;
	};

	double ComputeParitySectionWorldX(const FTDFluidWaterArmOutputs& Outputs, int32 ParityColumn)
	{
		return Outputs.GetMinimumCorner().X + (ParityColumn + 0.5) * Outputs.ExtentCm.X / ParityResolution;
	}

	FTDFluidWaterSectionFlux MeasureSectionFlux(const FTDFluidWaterArmOutputs& Outputs, const FTDFluidWaterGuideOracle& Oracle, double SectionWorldX)
	{
		FTDFluidWaterSectionFlux Flux;
		Flux.TargetFlux = Oracle.MeasureTargetFlux(SectionWorldX);
		const int32 Column = Outputs.FindColumn(SectionWorldX);
		if (Column == INDEX_NONE || !Outputs.HasAllPixels())
		{
			return Flux;
		}

		const double CellSizeY = Outputs.GetCellSizeCm().Y;
		for (int32 CellY = 0; CellY < Outputs.Resolution; ++CellY)
		{
			const int32 CellIndex = CellY * Outputs.Resolution + Column;
			if (Outputs.IsSolid(CellIndex))
			{
				continue;
			}
			const double VelocityX = Outputs.DecodeVelocity(CellIndex).X;
			Flux.NetFlux += VelocityX * CellSizeY;
			Flux.ReverseFlux += FMath::Max(0.0, -VelocityX) * CellSizeY;
		}
		return Flux;
	}

	TArray<FTDFluidWaterSectionFlux> MeasureDischargeSections(const FTDFluidWaterArmOutputs& Outputs, const FTDFluidWaterGuideOracle& Oracle)
	{
		TArray<FTDFluidWaterSectionFlux> Sections;
		for (const int32 ParityColumn : DischargeSectionColumns)
		{
			Sections.Add(MeasureSectionFlux(Outputs, Oracle, ComputeParitySectionWorldX(Outputs, ParityColumn)));
		}
		return Sections;
	}

	double ComputeFluxRatio(double FluxValue, double TargetFlux)
	{
		return TargetFlux > UE_DOUBLE_SMALL_NUMBER ? FluxValue / TargetFlux : 0.0;
	}

	void ReportSections(FAutomationTestBase& Test, const FString& ArmLabel, const TArray<FTDFluidWaterSectionFlux>& Sections)
	{
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			const FTDFluidWaterSectionFlux& Flux = Sections[SectionIndex];
			ReportLine(Test, FString::Printf(TEXT("TDFluidGpuWaterDischarge: %s section=%d net=%.1f target=%.1f reverse=%.2f ratio=%.4f"), *ArmLabel, DischargeSectionColumns[SectionIndex], Flux.NetFlux, Flux.TargetFlux, Flux.ReverseFlux, ComputeFluxRatio(Flux.NetFlux, Flux.TargetFlux)));
		}
	}

	void ExpectCpuDischarge(FAutomationTestBase& Test, const FString& ArmLabel, const TArray<FTDFluidWaterSectionFlux>& Sections)
	{
		double MeanFlux = 0.0;
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			const FTDFluidWaterSectionFlux& Flux = Sections[SectionIndex];
			const int32 SectionColumn = DischargeSectionColumns[SectionIndex];
			Test.TestTrue(*FString::Printf(TEXT("%s section %d target flux is positive"), *ArmLabel, SectionColumn), Flux.TargetFlux > 1000.0);
			Test.TestTrue(*FString::Printf(TEXT("%s section %d net flux is within 15 percent of the target flux"), *ArmLabel, SectionColumn), FMath::Abs(Flux.NetFlux - Flux.TargetFlux) <= 0.15 * Flux.TargetFlux);
			Test.TestTrue(*FString::Printf(TEXT("%s section %d reverse flux is under 5 percent of the target flux"), *ArmLabel, SectionColumn), Flux.ReverseFlux < 0.05 * Flux.TargetFlux);
			MeanFlux += Flux.NetFlux / Sections.Num();
		}
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			Test.TestTrue(*FString::Printf(TEXT("%s section %d net flux is within 10 percent of the section mean"), *ArmLabel, DischargeSectionColumns[SectionIndex]), FMath::Abs(Sections[SectionIndex].NetFlux - MeanFlux) <= 0.10 * FMath::Abs(MeanFlux));
		}
	}

	void ExpectDischargeParity(FAutomationTestBase& Test, const FString& ArmLabel, const TArray<FTDFluidWaterSectionFlux>& CpuSections, const TArray<FTDFluidWaterSectionFlux>& GpuSections)
	{
		if (!Test.TestEqual(*(ArmLabel + TEXT(" measured every discharge section")), GpuSections.Num(), CpuSections.Num()))
		{
			return;
		}

		for (int32 SectionIndex = 0; SectionIndex < CpuSections.Num(); ++SectionIndex)
		{
			const double TargetFlux = CpuSections[SectionIndex].TargetFlux;
			const double CpuRatio = ComputeFluxRatio(CpuSections[SectionIndex].NetFlux, TargetFlux);
			const double GpuRatio = ComputeFluxRatio(GpuSections[SectionIndex].NetFlux, TargetFlux);
			const double CpuReverseRatio = ComputeFluxRatio(CpuSections[SectionIndex].ReverseFlux, TargetFlux);
			const double GpuReverseRatio = ComputeFluxRatio(GpuSections[SectionIndex].ReverseFlux, TargetFlux);
			const int32 SectionColumn = DischargeSectionColumns[SectionIndex];
			Test.TestTrue(*FString::Printf(TEXT("%s section %d discharge ratio %.4f is within 0.03 of the CPU ratio %.4f"), *ArmLabel, SectionColumn, GpuRatio, CpuRatio), FMath::Abs(GpuRatio - CpuRatio) <= DischargeParityTolerance);
			Test.TestTrue(*FString::Printf(TEXT("%s section %d reverse ratio %.4f is at most the CPU ratio %.4f plus 0.01"), *ArmLabel, SectionColumn, GpuReverseRatio, CpuReverseRatio), GpuReverseRatio <= CpuReverseRatio + ReverseFluxParityMargin);
		}
	}

	struct FTDFluidWaterRiverArm
	{
		FTDFluidWaterArmOutputs Outputs;
		FTDFluidWaterGuideOracle Oracle;
		FTDFluidWaterTargetInfo Targets;
	};

	bool RunRiverArm(FAutomationTestBase& Test, const FString& ArmLabel, const FTDFluidWaterSceneSpec& SceneSpec, ETDFluidSolverBackend Backend, int32 StepCount, FTDFluidWaterRiverArm& OutArm)
	{
		return RunWaterActorArm(Test, ArmLabel, SceneSpec, Backend, [&OutArm, StepCount](FTDFluidWaterArmContext& ArmContext) -> bool
		{
			for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
			{
				ArmContext.Step();
			}
			OutArm.Outputs = ArmContext.Capture();
			OutArm.Oracle = ArmContext.BuildOracle();
			OutArm.Targets = ArmContext.DescribeTargets();
			return true;
		});
	}

	FTDFluidWaterSceneSpec MakeStraightRiverScene(int32 Resolution, float SceneStepSeconds)
	{
		return MakeRiverScene(Resolution, ParityExtentCm, SceneStepSeconds, MakeStraightRiver(-2700.0, 2700.0, 400.0f, 200.0f));
	}

	void ReportPressureVariables(FAutomationTestBase& Test, const FString& ArmLabel)
	{
		const IConsoleVariable* PressureModeVariable = IConsoleManager::Get().FindConsoleVariable(WaterPressureModeVariableName);
		const IConsoleVariable* PressureCyclesVariable = IConsoleManager::Get().FindConsoleVariable(GpuPressureCyclesVariableName);
		const IConsoleVariable* PostSmoothVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("TD.FluidFX.WaterGpuHalfPostSmoothIterations"));
		const IConsoleVariable* ClosedRhsMeanVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("TD.FluidFX.WaterGpuClosedHalfRhsMean"));
		ReportLine(Test, FString::Printf(TEXT("TDFluidGpuWaterPressure: %s pressure_mode=%s cycles=%s post_smooth=%s closed_rhs_mean=%s"), *ArmLabel, PressureModeVariable ? *PressureModeVariable->GetString() : TEXT("unregistered"), PressureCyclesVariable ? *PressureCyclesVariable->GetString() : TEXT("unregistered"), PostSmoothVariable ? *PostSmoothVariable->GetString() : TEXT("unregistered"), ClosedRhsMeanVariable ? *ClosedRhsMeanVariable->GetString() : TEXT("unregistered")));
	}

	bool RunOpenBorderDischargeParity(FAutomationTestBase& Test, const FString& CaseLabel, float SceneStepSeconds, int32 StepCount, int32 GpuResolution)
	{
		if (!CanRunWaterRhiTest(Test, CaseLabel))
		{
			return false;
		}

		FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
		FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
		const FString CpuLabel = CaseLabel + TEXT(" CPU 192");
		FTDFluidWaterRiverArm CpuArm;
		if (!RunRiverArm(Test, CpuLabel, MakeStraightRiverScene(ParityResolution, SceneStepSeconds), ETDFluidSolverBackend::Cpu, StepCount, CpuArm))
		{
			return false;
		}
		if (!Test.TestTrue(*(CpuLabel + TEXT(" captured every packed output pixel")), CpuArm.Outputs.HasAllPixels()))
		{
			return false;
		}

		const TArray<FTDFluidWaterSectionFlux> CpuSections = MeasureDischargeSections(CpuArm.Outputs, CpuArm.Oracle);
		ReportSections(Test, CpuLabel, CpuSections);
		ExpectCpuDischarge(Test, CpuLabel, CpuSections);
		if (!ExpectWaterGpuRegistered(Test, CaseLabel, WaterGpuScope))
		{
			return false;
		}

		FTDWaterGpuConsoleVariableScope VelocityFormatScope(WaterVelocityFormatVariableName, -1);
		const FString GpuLabel = FString::Printf(TEXT("%s GPU %d"), *CaseLabel, GpuResolution);
		FTDFluidWaterRiverArm GpuArm;
		if (!RunRiverArm(Test, GpuLabel, MakeStraightRiverScene(GpuResolution, SceneStepSeconds), ETDFluidSolverBackend::Gpu, StepCount, GpuArm))
		{
			return false;
		}
		if (!ExpectGpuArmOutputs(Test, GpuLabel, GpuArm.Targets, GpuArm.Outputs, GpuResolution))
		{
			return false;
		}
		if (!ExpectFlowActivity(Test, GpuLabel, GpuArm.Outputs, GpuArm.Oracle))
		{
			return false;
		}

		const TArray<FTDFluidWaterSectionFlux> GpuSections = MeasureDischargeSections(GpuArm.Outputs, GpuArm.Oracle);
		ReportSections(Test, GpuLabel, GpuSections);
		ReportPressureVariables(Test, GpuLabel);
		ExpectDischargeParity(Test, GpuLabel, CpuSections, GpuSections);
		if (GpuResolution != ParityResolution)
		{
			return true;
		}
		if (!VelocityFormatScope.IsValid())
		{
			ReportLine(Test, FString::Printf(TEXT("TDFluidGpuWaterDischarge: %s half float velocity record skipped because %s is not registered"), *CaseLabel, WaterVelocityFormatVariableName));
			return true;
		}

		VelocityFormatScope.SetValue(0);
		const FString HalfFloatLabel = CaseLabel + TEXT(" GPU 192 half float velocity (information)");
		FTDFluidWaterRiverArm HalfFloatArm;
		if (!RunRiverArm(Test, HalfFloatLabel, MakeStraightRiverScene(ParityResolution, SceneStepSeconds), ETDFluidSolverBackend::Gpu, StepCount, HalfFloatArm))
		{
			return false;
		}
		ReportSections(Test, HalfFloatLabel, MeasureDischargeSections(HalfFloatArm.Outputs, HalfFloatArm.Oracle));
		return true;
	}

	double MeasureCenterlineSpeed(const FTDFluidWaterArmOutputs& Outputs, double WorldX)
	{
		const int32 Column = Outputs.FindColumn(WorldX);
		if (Column == INDEX_NONE || !Outputs.HasAllPixels())
		{
			return 0.0;
		}

		const int32 CenterRow = Outputs.Resolution / 2;
		const double LowerSpeed = Outputs.DecodeVelocity((CenterRow - 1) * Outputs.Resolution + Column).X;
		const double UpperSpeed = Outputs.DecodeVelocity(CenterRow * Outputs.Resolution + Column).X;
		return 0.5 * (LowerSpeed + UpperSpeed);
	}

	FTDFluidWaterSceneSpec MakeNarrowingScene(int32 Resolution)
	{
		const TArray<double> PointsX = { -2700.0, -700.0, -400.0, 500.0, 800.0, 2700.0 };
		const TArray<float> WidthScales = { 1.0f, 1.0f, 0.5f, 0.5f, 1.0f, 1.0f };
		return MakeRiverScene(Resolution, ParityExtentCm, StepSeconds60, MakeProfiledRiver(PointsX, WidthScales, 400.0f, 200.0f));
	}

	struct FTDFluidWaterRockMetrics
	{
		bool bRockIsSolid = false;
		double RockSpeed = 0.0;
		double UpperFlankLateral = 0.0;
		double LowerFlankLateral = 0.0;
		double WakeWhitewater = 0.0;
		double UpstreamWhitewater = 0.0;
	};

	FTDFluidWaterRockMetrics MeasureRockMetrics(const FTDFluidWaterArmOutputs& Outputs, const FVector2D& RockCenter, double RockRadiusCm)
	{
		FTDFluidWaterRockMetrics Metrics;
		if (!Outputs.HasAllPixels())
		{
			return Metrics;
		}

		const int32 RockIndex = Outputs.FindCellIndex(RockCenter);
		if (RockIndex != INDEX_NONE)
		{
			Metrics.bRockIsSolid = Outputs.IsSolid(RockIndex);
			Metrics.RockSpeed = Outputs.DecodeVelocity(RockIndex).Size();
		}

		const double FlankOffset = (RockRadiusCm + 30.0) * FMath::Sqrt(0.5);
		const int32 UpperFlankIndex = Outputs.FindCellIndex(RockCenter + FVector2D(-FlankOffset, FlankOffset));
		const int32 LowerFlankIndex = Outputs.FindCellIndex(RockCenter + FVector2D(-FlankOffset, -FlankOffset));
		Metrics.UpperFlankLateral = UpperFlankIndex != INDEX_NONE ? Outputs.DecodeVelocity(UpperFlankIndex).Y : 0.0;
		Metrics.LowerFlankLateral = LowerFlankIndex != INDEX_NONE ? Outputs.DecodeVelocity(LowerFlankIndex).Y : 0.0;

		for (int32 CellY = 0; CellY < Outputs.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Outputs.Resolution; ++CellX)
			{
				const int32 CellIndex = CellY * Outputs.Resolution + CellX;
				if (Outputs.IsSolid(CellIndex))
				{
					continue;
				}

				const FVector2D CellWorld = Outputs.GetCellCenterWorld(CellX, CellY);
				const bool bIsInWake = CellWorld.X > RockCenter.X + RockRadiusCm && CellWorld.X <= RockCenter.X + RockRadiusCm + 300.0 && FMath::Abs(CellWorld.Y - RockCenter.Y) <= 2.0 * RockRadiusCm;
				if (bIsInWake)
				{
					Metrics.WakeWhitewater = FMath::Max(Metrics.WakeWhitewater, static_cast<double>(Outputs.GetDensity(CellIndex)));
				}
				else if (CellWorld.X < RockCenter.X - 3.0 * RockRadiusCm)
				{
					Metrics.UpstreamWhitewater = FMath::Max(Metrics.UpstreamWhitewater, static_cast<double>(Outputs.GetDensity(CellIndex)));
				}
			}
		}
		return Metrics;
	}

	FVector2D ComputeDensityCentroid(const FTDFluidWaterArmOutputs& Outputs, double& OutMass)
	{
		FVector2D WeightedSum = FVector2D::ZeroVector;
		OutMass = 0.0;
		for (int32 CellY = 0; CellY < Outputs.Resolution && Outputs.HasAllPixels(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Outputs.Resolution; ++CellX)
			{
				const double Density = Outputs.GetDensity(CellY * Outputs.Resolution + CellX);
				WeightedSum += Outputs.GetCellCenterWorld(CellX, CellY) * Density;
				OutMass += Density;
			}
		}
		return OutMass > UE_DOUBLE_SMALL_NUMBER ? WeightedSum / OutMass : FVector2D::ZeroVector;
	}

	struct FTDFluidWaterRing
	{
		double RadiusCells = 0.0;
		double PeakAmplitudeCm = 0.0;
	};

	TArray<double> DecodeRippleHeights(const FTDFluidWaterArmOutputs& Outputs, float RippleReferenceCm)
	{
		TArray<double> Heights;
		Heights.Init(0.0, Outputs.GetCellCount());
		for (int32 CellIndex = 0; CellIndex < Outputs.SurfacePixels.Num() && CellIndex < Heights.Num(); ++CellIndex)
		{
			Heights[CellIndex] = Outputs.DecodeRippleHeightCm(CellIndex, RippleReferenceCm);
		}
		return Heights;
	}

	FTDFluidWaterRing MeasureRingRadius(const TArray<double>& Heights, int32 Resolution, int32 CenterCellX, int32 CenterCellY)
	{
		FTDFluidWaterRing Ring;
		if (Resolution <= 0 || Heights.Num() != Resolution * Resolution)
		{
			return Ring;
		}

		TArray<double> BinSums;
		TArray<int32> BinSampleCounts;
		BinSums.Init(0.0, Resolution);
		BinSampleCounts.Init(0, Resolution);
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const double Distance = FMath::Sqrt(static_cast<double>(FMath::Square(CellX - CenterCellX) + FMath::Square(CellY - CenterCellY)));
				const int32 Bin = FMath::RoundToInt32(Distance);
				if (Bin >= Resolution)
				{
					continue;
				}
				BinSums[Bin] += Heights[CellY * Resolution + CellX];
				++BinSampleCounts[Bin];
			}
		}

		TArray<double> BinMeans;
		BinMeans.Init(0.0, Resolution);
		double LargestMagnitude = 0.0;
		for (int32 Bin = 1; Bin < Resolution; ++Bin)
		{
			BinMeans[Bin] = BinSampleCounts[Bin] > 0 ? BinSums[Bin] / BinSampleCounts[Bin] : 0.0;
			LargestMagnitude = FMath::Max(LargestMagnitude, FMath::Abs(BinMeans[Bin]));
		}

		int32 OuterBin = INDEX_NONE;
		for (int32 Bin = Resolution - 1; Bin >= 1; --Bin)
		{
			if (FMath::Abs(BinMeans[Bin]) >= 0.1 * LargestMagnitude)
			{
				OuterBin = Bin;
				break;
			}
		}
		if (OuterBin == INDEX_NONE || LargestMagnitude <= 0.0)
		{
			return Ring;
		}

		const bool bLeadingLobeIsPositive = BinMeans[OuterBin] > 0.0;
		double WeightSum = 0.0;
		double RadiusSum = 0.0;
		for (int32 Bin = OuterBin; Bin >= 1 && BinMeans[Bin] != 0.0 && (BinMeans[Bin] > 0.0) == bLeadingLobeIsPositive; --Bin)
		{
			const double Magnitude = FMath::Abs(BinMeans[Bin]);
			WeightSum += Magnitude;
			RadiusSum += Magnitude * Bin;
			Ring.PeakAmplitudeCm = FMath::Max(Ring.PeakAmplitudeCm, Magnitude);
		}
		Ring.RadiusCells = WeightSum > 0.0 ? RadiusSum / WeightSum : 0.0;
		return Ring;
	}

	double MeasureRippleEnergy(const FTDFluidWaterArmOutputs& Outputs, float RippleReferenceCm)
	{
		double Energy = 0.0;
		for (const double HeightCm : DecodeRippleHeights(Outputs, RippleReferenceCm))
		{
			Energy += FMath::Square(HeightCm);
		}
		return Energy;
	}

	double MeasureAdvectionAmplitude(const FTDFluidWaterArmOutputs& Outputs, float RippleReferenceCm)
	{
		double SquareSum = 0.0;
		int32 CellCount = 0;
		for (int32 CellY = 24; CellY < 40 && Outputs.HasAllPixels(); ++CellY)
		{
			for (int32 CellX = 24; CellX < 48; ++CellX)
			{
				SquareSum += FMath::Square(static_cast<double>(Outputs.DecodeRippleHeightCm(CellY * Outputs.Resolution + CellX, RippleReferenceCm)));
				++CellCount;
			}
		}
		return CellCount > 0 ? FMath::Sqrt(2.0 * SquareSum / CellCount) : 0.0;
	}

	double MeasureAverageSpeed(const FTDFluidWaterArmOutputs& Outputs)
	{
		double SpeedSum = 0.0;
		for (int32 CellIndex = 0; CellIndex < Outputs.GetCellCount() && Outputs.HasAllPixels(); ++CellIndex)
		{
			SpeedSum += Outputs.DecodeVelocity(CellIndex).Size();
		}
		return Outputs.GetCellCount() > 0 ? SpeedSum / Outputs.GetCellCount() : 0.0;
	}

	TArray<float> ComputeSolidDistanceCm(const FTDFluidWaterArmOutputs& Outputs, double MaximumDistanceCm)
	{
		const int32 Resolution = Outputs.Resolution;
		const FVector2D CellSize = Outputs.GetCellSizeCm();
		const int32 RangeX = FMath::CeilToInt32(MaximumDistanceCm / CellSize.X);
		const int32 RangeY = FMath::CeilToInt32(MaximumDistanceCm / CellSize.Y);
		const int32 FarCells = RangeX + 1;
		TArray<int32> RowDistanceCells;
		RowDistanceCells.Init(FarCells, Outputs.GetCellCount());
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			int32 LastSolidX = INDEX_NONE;
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const int32 CellIndex = CellY * Resolution + CellX;
				LastSolidX = Outputs.IsSolid(CellIndex) ? CellX : LastSolidX;
				RowDistanceCells[CellIndex] = LastSolidX == INDEX_NONE ? FarCells : FMath::Min(FarCells, CellX - LastSolidX);
			}

			LastSolidX = INDEX_NONE;
			for (int32 CellX = Resolution - 1; CellX >= 0; --CellX)
			{
				const int32 CellIndex = CellY * Resolution + CellX;
				LastSolidX = Outputs.IsSolid(CellIndex) ? CellX : LastSolidX;
				if (LastSolidX != INDEX_NONE)
				{
					RowDistanceCells[CellIndex] = FMath::Min(RowDistanceCells[CellIndex], LastSolidX - CellX);
				}
			}
		}

		const double FarDistanceSquared = FMath::Square(MaximumDistanceCm + 1.0);
		TArray<float> Distances;
		Distances.Init(static_cast<float>(MaximumDistanceCm + 1.0), Outputs.GetCellCount());
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				double BestDistanceSquared = FarDistanceSquared;
				for (int32 OffsetY = -RangeY; OffsetY <= RangeY; ++OffsetY)
				{
					const int32 NeighborY = CellY + OffsetY;
					if (NeighborY < 0 || NeighborY >= Resolution)
					{
						continue;
					}

					const int32 RowDistance = RowDistanceCells[NeighborY * Resolution + CellX];
					if (RowDistance > RangeX)
					{
						continue;
					}
					BestDistanceSquared = FMath::Min(BestDistanceSquared, FMath::Square(RowDistance * CellSize.X) + FMath::Square(OffsetY * CellSize.Y));
				}
				if (BestDistanceSquared < FarDistanceSquared)
				{
					Distances[CellY * Resolution + CellX] = static_cast<float>(FMath::Sqrt(BestDistanceSquared));
				}
			}
		}
		return Distances;
	}

	struct FTDFluidWaterDivergence
	{
		double InteriorL2 = 0.0;
		int32 InteriorCellCount = 0;
		double BandL2 = 0.0;
		int32 BandCellCount = 0;
	};

	FTDFluidWaterDivergence MeasureOutputDivergence(const FTDFluidWaterArmOutputs& Outputs, double BorderBandCm)
	{
		FTDFluidWaterDivergence Divergence;
		if (!Outputs.HasAllPixels() || Outputs.Resolution < 3)
		{
			return Divergence;
		}

		const int32 Resolution = Outputs.Resolution;
		const FVector2D CellSize = Outputs.GetCellSizeCm();
		const FVector2D MinimumCorner = Outputs.GetMinimumCorner();
		const FVector2D MaximumCorner = MinimumCorner + Outputs.ExtentCm;
		const TArray<float> SolidDistances = ComputeSolidDistanceCm(Outputs, SolidBandDistanceCm);
		double InteriorSquaredSum = 0.0;
		double BandSquaredSum = 0.0;
		for (int32 CellY = 1; CellY < Resolution - 1; ++CellY)
		{
			for (int32 CellX = 1; CellX < Resolution - 1; ++CellX)
			{
				const int32 CellIndex = CellY * Resolution + CellX;
				if (Outputs.IsSolid(CellIndex))
				{
					continue;
				}

				const FVector2D CellWorld = Outputs.GetCellCenterWorld(CellX, CellY);
				const double EdgeDistanceCm = FMath::Min(FMath::Min(CellWorld.X - MinimumCorner.X, MaximumCorner.X - CellWorld.X), FMath::Min(CellWorld.Y - MinimumCorner.Y, MaximumCorner.Y - CellWorld.Y));
				if (EdgeDistanceCm < BorderBandCm)
				{
					continue;
				}

				const double DerivativeX = (Outputs.DecodeVelocity(CellIndex + 1).X - Outputs.DecodeVelocity(CellIndex - 1).X) / (2.0 * CellSize.X);
				const double DerivativeY = (Outputs.DecodeVelocity(CellIndex + Resolution).Y - Outputs.DecodeVelocity(CellIndex - Resolution).Y) / (2.0 * CellSize.Y);
				const double CellDivergence = DerivativeX + DerivativeY;
				if (SolidDistances[CellIndex] > SolidBandDistanceCm)
				{
					InteriorSquaredSum += CellDivergence * CellDivergence;
					++Divergence.InteriorCellCount;
					continue;
				}
				BandSquaredSum += CellDivergence * CellDivergence;
				++Divergence.BandCellCount;
			}
		}

		Divergence.InteriorL2 = Divergence.InteriorCellCount > 0 ? FMath::Sqrt(InteriorSquaredSum / Divergence.InteriorCellCount) : 0.0;
		Divergence.BandL2 = Divergence.BandCellCount > 0 ? FMath::Sqrt(BandSquaredSum / Divergence.BandCellCount) : 0.0;
		return Divergence;
	}

	FTDFluidWaterSceneSpec MakeRockRiverScene(int32 Resolution, const FVector2D& RockCenter)
	{
		FTDFluidWaterSceneSpec SceneSpec = MakeStraightRiverScene(Resolution, StepSeconds60);
		FTDFluidWaterRockSpec Rock;
		Rock.Center = RockCenter;
		Rock.RadiusCm = 70.0f;
		SceneSpec.Rocks.Add(Rock);
		return SceneSpec;
	}

	FTDFluidWaterSceneSpec MakeScenarioRiverScene(int32 Resolution, double ExtentCm)
	{
		FTDFluidWaterSceneSpec SceneSpec = MakeRiverScene(Resolution, ExtentCm, StepSeconds60, MakeStraightRiver(-6000.0, 6000.0, 400.0f, 200.0f));
		SceneSpec.Config.RippleWaveSpeedCmPerSec = 120.0f;
		SceneSpec.Config.RippleDampingPerSec = 1.2f;
		SceneSpec.Config.RippleFlowAdvection = 1.0f;
		SceneSpec.Config.RapidsWhitewaterPerSec = 40.0f;
		SceneSpec.Config.WhitewaterFromVorticity = 0.3f;
		FTDFluidWaterRockSpec Rock;
		Rock.Center = FVector2D(200.0, 0.0);
		Rock.RadiusCm = 70.0f;
		SceneSpec.Rocks.Add(Rock);
		return SceneSpec;
	}

	constexpr double BankInnerEdgeCm = 400.0;
	constexpr double BankTopZCm = 100.0;

	FTDFluidWaterSceneSpec MakeDryBankScene()
	{
		FTDFluidWaterSceneSpec SceneSpec;
		SceneSpec.Config = MakeWaterArmConfig(ParityResolution, ParityExtentCm);
		SceneSpec.Blocks.Add(MakeFloorBlock());
		for (const double BankSide : { 1.0, -1.0 })
		{
			FTDFluidWaterBlockSpec Bank;
			Bank.Center = FVector(0.0, BankSide * (BankInnerEdgeCm + 1500.0), BankTopZCm - CubeHalfSizeCm);
			Bank.Scale = FVector(80.0, 30.0, 1.0);
			SceneSpec.Blocks.Add(Bank);
		}
		SceneSpec.Regions.Add(MakeRegion(FVector2D(-5000.0, -5000.0), FVector2D(5000.0, 5000.0), WaterSurfaceZCm));
		SceneSpec.bWaitsForTerrain = false;
		return SceneSpec;
	}

	struct FTDFluidWaterDryBankArm
	{
		int32 UnknownCellObservations = 0;
		int32 UnknownSolidObservations = 0;
		FTDFluidWaterArmOutputs FinalOutputs;
		FTDFluidWaterTargetInfo Targets;
	};

	bool RunDryBankArm(FAutomationTestBase& Test, const FString& ArmLabel, ETDFluidSolverBackend Backend, FTDFluidWaterDryBankArm& OutArm)
	{
		const FTDFluidWaterSceneSpec SceneSpec = MakeDryBankScene();
		return RunWaterActorArm(Test, ArmLabel, SceneSpec, Backend, [&Test, &ArmLabel, &OutArm](FTDFluidWaterArmContext& ArmContext) -> bool
		{
			const auto ObserveUnknownCells = [&ArmContext, &OutArm]()
			{
				const FTDFluidWaterArmOutputs Outputs = ArmContext.Capture();
				if (!Outputs.HasAllPixels())
				{
					return;
				}

				const UTDFluidLiveComponent& Component = ArmContext.GetComponent();
				for (int32 CellY = 0; CellY < Outputs.Resolution; ++CellY)
				{
					for (int32 CellX = 0; CellX < Outputs.Resolution; ++CellX)
					{
						const int32 CellIndex = CellY * Outputs.Resolution + CellX;
						const bool bIsUnknown = Outputs.bUsesGpuSolver ? Outputs.IsTerrainUnknown(CellIndex) : !Component.GetSolver().SampleAtCell(CellX, CellY).bIsTerrainKnown;
						if (!bIsUnknown)
						{
							continue;
						}
						++OutArm.UnknownCellObservations;
						OutArm.UnknownSolidObservations += Outputs.IsSolid(CellIndex) ? 1 : 0;
					}
				}
			};

			ObserveUnknownCells();
			if (!WaitForTerrainSampling(Test, ArmLabel, ArmContext, [&ObserveUnknownCells](int32) { ObserveUnknownCells(); }))
			{
				return false;
			}

			ArmContext.Step();
			OutArm.FinalOutputs = ArmContext.Capture();
			OutArm.Targets = ArmContext.DescribeTargets();
			return true;
		});
	}

	bool IsDryBankCell(const FTDFluidWaterArmOutputs& Outputs, int32 CellX, int32 CellY)
	{
		return FMath::Abs(Outputs.GetCellCenterWorld(CellX, CellY).Y) >= BankInnerEdgeCm + ParityCellSizeCm;
	}

	void ExpectDryBankCells(FAutomationTestBase& Test, const FString& ArmLabel, const FTDFluidWaterDryBankArm& Arm)
	{
		int32 DryCellCount = 0;
		int32 DryCellsWithAlpha = 0;
		int32 DryCellsWithDepth = 0;
		const FTDFluidWaterArmOutputs& Outputs = Arm.FinalOutputs;
		for (int32 CellY = 0; CellY < Outputs.Resolution && Outputs.HasAllPixels(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Outputs.Resolution; ++CellX)
			{
				if (!IsDryBankCell(Outputs, CellX, CellY))
				{
					continue;
				}
				const int32 CellIndex = CellY * Outputs.Resolution + CellX;
				++DryCellCount;
				DryCellsWithAlpha += Outputs.IsSolid(CellIndex) ? 0 : 1;
				DryCellsWithDepth += Outputs.GetDepthEncoding(CellIndex) > 0.0f ? 1 : 0;
			}
		}

		ReportLine(Test, FString::Printf(TEXT("TDFluidGpuWaterDryBank: %s unknown_observations=%d unknown_solid=%d dry_cells=%d dry_with_alpha=%d dry_with_depth=%d"), *ArmLabel, Arm.UnknownCellObservations, Arm.UnknownSolidObservations, DryCellCount, DryCellsWithAlpha, DryCellsWithDepth));
		Test.TestTrue(*(ArmLabel + TEXT(" observed cells with unknown terrain while sampling")), Arm.UnknownCellObservations > 0);
		Test.TestEqual(*(ArmLabel + TEXT(" cells with unknown terrain that were solid while sampling")), Arm.UnknownSolidObservations, 0);
		Test.TestTrue(*(ArmLabel + TEXT(" has dry bank cells")), DryCellCount > 0);
		Test.TestEqual(*(ArmLabel + TEXT(" dry bank cells with a nonzero density alpha one step after terrain completion")), DryCellsWithAlpha, 0);
		Test.TestEqual(*(ArmLabel + TEXT(" dry bank cells with a nonzero depth one step after terrain completion")), DryCellsWithDepth, 0);
	}

	FTDFluidWaterSceneSpec MakeTiltedBedScene()
	{
		constexpr double BedSlope = 0.1;
		constexpr double BedZAtOriginCm = -250.0;
		const double PitchRadians = FMath::Atan(BedSlope);
		FTDFluidWaterSceneSpec SceneSpec;
		SceneSpec.Config = MakeWaterArmConfig(ParityResolution, ParityExtentCm);
		SceneSpec.Config.TerrainSlopeGravity = 400.0f;
		FTDFluidWaterBlockSpec TiltedBed;
		TiltedBed.Rotation = FRotator(FMath::RadiansToDegrees(PitchRadians), 0.0, 0.0);
		TiltedBed.Scale = FVector(50.0, 50.0, 1.0);
		TiltedBed.Center = FVector(0.0, 0.0, BedZAtOriginCm - CubeHalfSizeCm / FMath::Cos(PitchRadians));
		SceneSpec.Blocks.Add(TiltedBed);
		SceneSpec.Regions.Add(MakeRegion(FVector2D(-5000.0, -5000.0), FVector2D(5000.0, 5000.0), WaterSurfaceZCm));
		return SceneSpec;
	}

	struct FTDFluidWaterProbeMetrics
	{
		double FlowRelativeErrors[3] = { 1.0, 1.0, 1.0 };
		double WideSpeed = 0.0;
		double RiffleSpeed = 0.0;
		double SpeedRatio = 0.0;
		double RockWakeWhitewater = 0.0;
		double UpperFlankLateral = 0.0;
		double LowerFlankLateral = 0.0;
		double PawnRippleCm = 0.0;
		double FoamShiftCm = 0.0;
		double FoamStartMass = 0.0;

		bool PassesFlowSpeed() const
		{
			return FlowRelativeErrors[0] <= 0.25 && FlowRelativeErrors[1] <= 0.25 && FlowRelativeErrors[2] <= 0.25;
		}

		bool PassesRiffle() const
		{
			return SpeedRatio >= 1.5;
		}

		bool PassesRockWake() const
		{
			return RockWakeWhitewater >= 0.3;
		}

		bool PassesPawnRipple() const
		{
			return PawnRippleCm >= 1.0;
		}

		bool PassesFoamDrift() const
		{
			return FoamStartMass >= 1.0 && FoamShiftCm >= 150.0;
		}
	};

	constexpr double ProbeRockCenterX = -300.0;
	constexpr double ProbeRockRadiusCm = 70.0;
	constexpr double ProbeWidePointsX[] = { -1600.0, -1300.0, -1000.0 };
	constexpr double ProbeRifflePointsX[] = { 900.0, 1000.0, 1100.0 };
	constexpr double ProbeWakeDistancesCm[] = { 60.0, 120.0, 180.0, 240.0, 300.0 };
	constexpr double ProbeWakeLateralsCm[] = { 0.0, 40.0, -40.0 };
	constexpr int32 ProbeSettleStepCount = 480;
	constexpr int32 ProbeMeasureSampleCount = 30;
	constexpr int32 ProbeStepsPerSample = 2;
	constexpr int32 ProbeFoamDriftStepCount = 120;
	constexpr double ProbeFoamStampX = -1600.0;
	constexpr double ProbeFoamRegionMinX = -1800.0;
	constexpr double ProbeFoamRegionMaxX = -450.0;
	constexpr double ProbeFoamRegionHalfWidthCm = 350.0;
	constexpr double ProbeFoamExpectedTravelCm = 400.0;
	constexpr double ProbePawnCrossingX = -1450.0;
	constexpr double ProbePawnStartY = -650.0;
	constexpr double ProbePawnEndY = 650.0;
	constexpr int32 ProbePawnStepCount = 240;
	constexpr double ProbePawnInWaterHalfWidthCm = 350.0;
	constexpr double ProbePawnRippleRadiusCm = 100.0;

	FTDFluidWaterSceneSpec MakeProbeScene(int32 Resolution)
	{
		const TArray<double> PointsX = { -2700.0, 500.0, 700.0, 1300.0, 1500.0, 2700.0 };
		const TArray<float> WidthScales = { 1.0f, 1.0f, 0.5f, 0.5f, 1.0f, 1.0f };
		FTDFluidWaterSceneSpec SceneSpec = MakeRiverScene(Resolution, ParityExtentCm, StepSeconds60, MakeProfiledRiver(PointsX, WidthScales, 400.0f, 200.0f));
		SceneSpec.Config.FlowNoiseAccelerationCmPerSec2 = 150.0f;
		SceneSpec.Config.RapidsWhitewaterPerSec = 2.0f;
		SceneSpec.Config.WhitewaterFromVorticity = 0.3f;
		SceneSpec.Config.RippleWaveSpeedCmPerSec = 120.0f;
		SceneSpec.Config.RippleDampingPerSec = 1.2f;
		SceneSpec.Config.RippleFlowAdvection = 1.0f;
		SceneSpec.Config.BankWetHeightCm = 10.0f;
		FTDFluidWaterRockSpec Rock;
		Rock.Center = FVector2D(ProbeRockCenterX, 0.0);
		Rock.RadiusCm = static_cast<float>(ProbeRockRadiusCm);
		SceneSpec.Rocks.Add(Rock);
		return SceneSpec;
	}

	double SampleSpeedAt(const FTDFluidWaterArmOutputs& Outputs, const FVector2D& WorldPoint)
	{
		const int32 CellIndex = Outputs.FindCellIndex(WorldPoint);
		return CellIndex != INDEX_NONE ? Outputs.DecodeVelocity(CellIndex).Size() : 0.0;
	}

	FVector2D ComputeExcessCentroid(const FTDFluidWaterArmOutputs& Outputs, const TArray<FFloat16Color>& BaselineDensity, double& OutMass)
	{
		FVector2D WeightedSum = FVector2D::ZeroVector;
		OutMass = 0.0;
		for (int32 CellY = 0; CellY < Outputs.Resolution && Outputs.HasAllPixels() && BaselineDensity.Num() == Outputs.GetCellCount(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Outputs.Resolution; ++CellX)
			{
				const FVector2D CellWorld = Outputs.GetCellCenterWorld(CellX, CellY);
				if (CellWorld.X < ProbeFoamRegionMinX || CellWorld.X > ProbeFoamRegionMaxX || FMath::Abs(CellWorld.Y) > ProbeFoamRegionHalfWidthCm)
				{
					continue;
				}
				const int32 CellIndex = CellY * Outputs.Resolution + CellX;
				const double Excess = FMath::Max(0.0, static_cast<double>(Outputs.GetDensity(CellIndex)) - static_cast<double>(BaselineDensity[CellIndex].R.GetFloat()));
				WeightedSum += CellWorld * Excess;
				OutMass += Excess;
			}
		}
		return OutMass > UE_DOUBLE_SMALL_NUMBER ? WeightedSum / OutMass : FVector2D::ZeroVector;
	}

	double MeasurePawnRipple(const FTDFluidWaterArmOutputs& Outputs, const FVector2D& PawnLocation, float RippleReferenceCm)
	{
		double MaximumHeightCm = 0.0;
		for (int32 CellY = 0; CellY < Outputs.Resolution && Outputs.HasAllPixels(); ++CellY)
		{
			for (int32 CellX = 0; CellX < Outputs.Resolution; ++CellX)
			{
				if (FVector2D::Distance(Outputs.GetCellCenterWorld(CellX, CellY), PawnLocation) > ProbePawnRippleRadiusCm)
				{
					continue;
				}
				MaximumHeightCm = FMath::Max(MaximumHeightCm, static_cast<double>(FMath::Abs(Outputs.DecodeRippleHeightCm(CellY * Outputs.Resolution + CellX, RippleReferenceCm))));
			}
		}
		return MaximumHeightCm;
	}

	struct FTDFluidWaterProbeArm
	{
		FTDFluidWaterProbeMetrics Metrics;
		FTDFluidWaterArmOutputs FlowOutputs;
		FTDFluidWaterGuideOracle Oracle;
		FTDFluidWaterTargetInfo Targets;
	};

	bool RunProbeArm(FAutomationTestBase& Test, const FString& ArmLabel, int32 Resolution, ETDFluidSolverBackend Backend, FTDFluidWaterProbeArm& OutArm)
	{
		const FTDFluidWaterSceneSpec SceneSpec = MakeProbeScene(Resolution);
		return RunWaterActorArm(Test, ArmLabel, SceneSpec, Backend, [&OutArm](FTDFluidWaterArmContext& ArmContext) -> bool
		{
			FTDFluidWaterProbeMetrics& Metrics = OutArm.Metrics;
			for (int32 StepIndex = 0; StepIndex < ProbeSettleStepCount; ++StepIndex)
			{
				ArmContext.Step();
			}

			OutArm.Oracle = ArmContext.BuildOracle();
			double WideSpeedSums[3] = {};
			double RiffleSpeedSums[3] = {};
			double WakeDensitySums[UE_ARRAY_COUNT(ProbeWakeDistancesCm) * UE_ARRAY_COUNT(ProbeWakeLateralsCm)] = {};
			double UpperFlankSum = 0.0;
			double LowerFlankSum = 0.0;
			const FVector2D RockCenter(ProbeRockCenterX, 0.0);
			const double FlankOffset = (ProbeRockRadiusCm + 30.0) * FMath::Sqrt(0.5);
			for (int32 SampleIndex = 0; SampleIndex < ProbeMeasureSampleCount; ++SampleIndex)
			{
				for (int32 StepIndex = 0; StepIndex < ProbeStepsPerSample; ++StepIndex)
				{
					ArmContext.Step();
				}

				const FTDFluidWaterArmOutputs Outputs = ArmContext.Capture();
				for (int32 PointIndex = 0; PointIndex < 3; ++PointIndex)
				{
					WideSpeedSums[PointIndex] += SampleSpeedAt(Outputs, FVector2D(ProbeWidePointsX[PointIndex], 0.0));
					RiffleSpeedSums[PointIndex] += SampleSpeedAt(Outputs, FVector2D(ProbeRifflePointsX[PointIndex], 0.0));
				}

				int32 WakeIndex = 0;
				for (const double WakeDistance : ProbeWakeDistancesCm)
				{
					for (const double WakeLateral : ProbeWakeLateralsCm)
					{
						const int32 CellIndex = Outputs.FindCellIndex(RockCenter + FVector2D(ProbeRockRadiusCm + WakeDistance, WakeLateral));
						WakeDensitySums[WakeIndex] += CellIndex != INDEX_NONE ? Outputs.GetDensity(CellIndex) : 0.0;
						++WakeIndex;
					}
				}

				const int32 UpperFlankIndex = Outputs.FindCellIndex(RockCenter + FVector2D(-FlankOffset, FlankOffset));
				const int32 LowerFlankIndex = Outputs.FindCellIndex(RockCenter + FVector2D(-FlankOffset, -FlankOffset));
				UpperFlankSum += UpperFlankIndex != INDEX_NONE ? Outputs.DecodeVelocity(UpperFlankIndex).Y : 0.0;
				LowerFlankSum += LowerFlankIndex != INDEX_NONE ? Outputs.DecodeVelocity(LowerFlankIndex).Y : 0.0;
				OutArm.FlowOutputs = Outputs;
			}

			double WideSpeedTotal = 0.0;
			double RiffleSpeedTotal = 0.0;
			for (int32 PointIndex = 0; PointIndex < 3; ++PointIndex)
			{
				const double WideMean = WideSpeedSums[PointIndex] / ProbeMeasureSampleCount;
				const int32 OracleIndex = OutArm.Oracle.FindCellIndex(FVector2D(ProbeWidePointsX[PointIndex], 0.0));
				const double TargetSpeed = OracleIndex != INDEX_NONE ? OutArm.Oracle.Targets[OracleIndex].Size() : 0.0;
				Metrics.FlowRelativeErrors[PointIndex] = TargetSpeed > UE_DOUBLE_SMALL_NUMBER ? FMath::Abs(WideMean - TargetSpeed) / TargetSpeed : 1.0;
				WideSpeedTotal += WideMean;
				RiffleSpeedTotal += RiffleSpeedSums[PointIndex] / ProbeMeasureSampleCount;
			}
			Metrics.WideSpeed = WideSpeedTotal / 3.0;
			Metrics.RiffleSpeed = RiffleSpeedTotal / 3.0;
			Metrics.SpeedRatio = Metrics.WideSpeed > UE_DOUBLE_SMALL_NUMBER ? Metrics.RiffleSpeed / Metrics.WideSpeed : 0.0;
			for (const double WakeDensitySum : WakeDensitySums)
			{
				Metrics.RockWakeWhitewater = FMath::Max(Metrics.RockWakeWhitewater, WakeDensitySum / ProbeMeasureSampleCount);
			}
			Metrics.UpperFlankLateral = UpperFlankSum / ProbeMeasureSampleCount;
			Metrics.LowerFlankLateral = LowerFlankSum / ProbeMeasureSampleCount;

			const TArray<FFloat16Color> BaselineDensity = ArmContext.Capture().DensityPixels;
			FTDFluidBrushStamp FoamStamp;
			FoamStamp.WorldLocation = FVector(ProbeFoamStampX, 0.0, 0.0);
			FoamStamp.RadiusCm = 90.0f;
			FoamStamp.DensityStrength = 1.0f;
			FoamStamp.VelocityStrength = 0.0f;
			FoamStamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
			ArmContext.QueueStamp(FoamStamp);
			ArmContext.Step();
			double FoamEndMass = 0.0;
			const FVector2D FoamStart = ComputeExcessCentroid(ArmContext.Capture(), BaselineDensity, Metrics.FoamStartMass);
			for (int32 StepIndex = 0; StepIndex < ProbeFoamDriftStepCount; ++StepIndex)
			{
				ArmContext.Step();
			}
			const FVector2D FoamEnd = ComputeExcessCentroid(ArmContext.Capture(), BaselineDensity, FoamEndMass);
			Metrics.FoamShiftCm = FoamEndMass > UE_DOUBLE_SMALL_NUMBER ? FoamEnd.X - FoamStart.X : 0.0;

			const double PawnSpeedCmPerSec = (ProbePawnEndY - ProbePawnStartY) / ((ProbePawnStepCount - 1) * StepSeconds60);
			for (int32 StepIndex = 0; StepIndex < ProbePawnStepCount; ++StepIndex)
			{
				const double PawnY = ProbePawnStartY + (ProbePawnEndY - ProbePawnStartY) * StepIndex / (ProbePawnStepCount - 1);
				FTDFluidBrushStamp PawnStamp;
				PawnStamp.WorldLocation = FVector(ProbePawnCrossingX, PawnY, 0.0);
				PawnStamp.WorldVelocity = FVector(0.0, PawnSpeedCmPerSec, 0.0);
				PawnStamp.RadiusCm = 65.0f;
				PawnStamp.DensityStrength = 0.45f;
				PawnStamp.VelocityStrength = 1.0f;
				PawnStamp.RippleImpulseCm = 2.5f;
				PawnStamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
				ArmContext.QueueStamp(PawnStamp);
				ArmContext.Step();
				if (FMath::Abs(PawnY) > ProbePawnInWaterHalfWidthCm)
				{
					continue;
				}
				Metrics.PawnRippleCm = FMath::Max(Metrics.PawnRippleCm, MeasurePawnRipple(ArmContext.Capture(), FVector2D(ProbePawnCrossingX, PawnY), DefaultRippleReferenceCm));
			}

			OutArm.Targets = ArmContext.DescribeTargets();
			return true;
		});
	}

	void ReportProbeMetrics(FAutomationTestBase& Test, const FString& ArmLabel, const FTDFluidWaterProbeMetrics& Metrics)
	{
		ReportLine(Test, FString::Printf(
			TEXT("TDFluidGpuWaterProbe: %s flow_errors=%.3f,%.3f,%.3f wide=%.2f riffle=%.2f ratio=%.3f wake=%.3f flank_upper=%.2f flank_lower=%.2f pawn_ripple_cm=%.3f foam_shift_cm=%.1f foam_mass=%.2f"),
			*ArmLabel,
			Metrics.FlowRelativeErrors[0],
			Metrics.FlowRelativeErrors[1],
			Metrics.FlowRelativeErrors[2],
			Metrics.WideSpeed,
			Metrics.RiffleSpeed,
			Metrics.SpeedRatio,
			Metrics.RockWakeWhitewater,
			Metrics.UpperFlankLateral,
			Metrics.LowerFlankLateral,
			Metrics.PawnRippleCm,
			Metrics.FoamShiftCm,
			Metrics.FoamStartMass));
	}

	void ExpectProbeVerdictsPass(FAutomationTestBase& Test, const FString& ArmLabel, const FTDFluidWaterProbeMetrics& Metrics)
	{
		Test.TestTrue(*(ArmLabel + TEXT(" flow speed at three wide points is within 25 percent of the target")), Metrics.PassesFlowSpeed());
		Test.TestTrue(*(ArmLabel + TEXT(" riffle to wide speed ratio is at least 1.5")), Metrics.PassesRiffle());
		Test.TestTrue(*(ArmLabel + TEXT(" rock wake whitewater within 3 m reaches 0.3")), Metrics.PassesRockWake());
		Test.TestTrue(*(ArmLabel + TEXT(" pawn crossing ripple reaches 1 cm within 1 m")), Metrics.PassesPawnRipple());
		Test.TestTrue(*(ArmLabel + TEXT(" foam drifts at least 150 cm downstream")), Metrics.PassesFoamDrift());
	}

	struct FTDFluidPoolUsage
	{
		uint32 WholePoolKb = 0;
		uint32 UsedKb = 0;
	};

	FTDFluidPoolUsage ReadPoolUsage()
	{
		FlushRenderingCommands();
		FTDFluidPoolUsage Usage;
		uint32 ElementCount = 0;
		GRenderTargetPool.GetStats(ElementCount, Usage.WholePoolKb, Usage.UsedKb);
		return Usage;
	}

	FRDGTextureDesc MakeStateArrayDesc(int32 Resolution, EPixelFormat Format)
	{
		return FRDGTextureDesc::Create2DArray(FIntPoint(Resolution, Resolution), Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, 1);
	}

	FRDGTextureDesc MakeOutputTargetDesc(int32 Resolution)
	{
		return FRDGTextureDesc::Create2D(FIntPoint(Resolution, Resolution), PF_FloatRGBA, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource);
	}

	int64 SumPlatformBytes(const TArray<FRDGTextureDesc>& Descs)
	{
		int64 TotalBytes = 0;
		for (const FRDGTextureDesc& Desc : Descs)
		{
			TotalBytes += static_cast<int64>(RHICalcTexturePlatformSize(Desc).Size);
		}
		return TotalBytes;
	}

	int64 SumLinearBytes(const TArray<FRDGTextureDesc>& Descs)
	{
		int64 TotalBytes = 0;
		for (const FRDGTextureDesc& Desc : Descs)
		{
			TotalBytes += static_cast<int64>(GPixelFormats[Desc.Format].BlockBytes) * Desc.Extent.X * Desc.Extent.Y * Desc.ArraySize;
		}
		return TotalBytes;
	}

	TArray<FRDGTextureDesc> MakeNonWaterStateDescs(int32 Resolution)
	{
		return {
			MakeStateArrayDesc(Resolution, PF_G16R16F),
			MakeStateArrayDesc(Resolution, PF_G16R16F),
			MakeStateArrayDesc(Resolution, PF_G32R32F),
			MakeStateArrayDesc(Resolution, PF_G32R32F),
			MakeStateArrayDesc(Resolution, PF_R16F),
			MakeStateArrayDesc(Resolution, PF_R16F),
			MakeStateArrayDesc(Resolution, PF_R32_FLOAT),
			MakeStateArrayDesc(Resolution, PF_R32_FLOAT),
			MakeStateArrayDesc(Resolution, PF_R16F),
			MakeStateArrayDesc(Resolution, PF_R32_FLOAT),
		};
	}

	int32 ComputeWaterFieldResolution(int32 Resolution)
	{
		if (Resolution <= 256)
		{
			return Resolution;
		}
		return Resolution < 1024 ? Resolution / 2 : Resolution / 4;
	}

	TArray<FRDGTextureDesc> MakeWaterStateDescs(int32 Resolution)
	{
		const bool bUsesFloat32State = Resolution <= 256;
		const EPixelFormat VelocityFormat = PF_G32R32F;
		const EPixelFormat RippleFormat = bUsesFloat32State ? PF_G32R32F : PF_G16R16F;
		const int32 PressureResolution = bUsesFloat32State ? Resolution : Resolution / 2;
		const int32 FieldResolution = ComputeWaterFieldResolution(Resolution);
		return {
			MakeStateArrayDesc(Resolution, VelocityFormat),
			MakeStateArrayDesc(Resolution, VelocityFormat),
			MakeStateArrayDesc(Resolution, PF_G32R32F),
			MakeStateArrayDesc(Resolution, PF_G32R32F),
			MakeStateArrayDesc(Resolution, RippleFormat),
			MakeStateArrayDesc(Resolution, RippleFormat),
			MakeStateArrayDesc(PressureResolution, PF_R32_FLOAT),
			MakeStateArrayDesc(PressureResolution, PF_R32_FLOAT),
			MakeStateArrayDesc(Resolution, PF_R16F),
			MakeStateArrayDesc(Resolution, PF_R32_FLOAT),
			MakeStateArrayDesc(FieldResolution, PF_A32B32G32R32F),
			MakeStateArrayDesc(FieldResolution, PF_R32_FLOAT),
			MakeStateArrayDesc(FieldResolution, PF_R32_FLOAT),
		};
	}

	TArray<FRDGTextureDesc> MakeWaterTransientDescs(int32 Resolution)
	{
		TArray<FRDGTextureDesc> Descs = {
			MakeStateArrayDesc(Resolution, PF_R16F),
			MakeStateArrayDesc(ComputeWaterFieldResolution(Resolution), PF_R16F),
		};
		Descs.Add(Resolution <= 256 ? MakeStateArrayDesc(Resolution, PF_R16F) : MakeStateArrayDesc(Resolution, PF_R32_FLOAT));
		return Descs;
	}

	int64 GetTableLinearStateBytes(int32 Resolution)
	{
		return Resolution <= 256 ? (Resolution == 192 ? 3170304 : 5636096) : (Resolution == 512 ? 14155776 : 51904512);
	}

	FTDFluidWaterSceneSpec MakeNoSurfaceStripScene(int32 Resolution)
	{
		const double FieldCellSizeCm = ParityExtentCm / ComputeWaterFieldResolution(Resolution);
		FTDFluidWaterSceneSpec SceneSpec;
		SceneSpec.Config = MakeWaterArmConfig(Resolution, ParityExtentCm);
		SceneSpec.Blocks.Add(MakeFloorBlock());
		SceneSpec.Regions.Add(MakeRegion(FVector2D(-5000.0, -5000.0), FVector2D(0.0, 5000.0), WaterSurfaceZCm));
		SceneSpec.Regions.Add(MakeRegion(FVector2D(FieldCellSizeCm, -5000.0), FVector2D(5000.0, 5000.0), WaterSurfaceZCm));
		return SceneSpec;
	}

	TArray<bool> MakeStripRegionMask(const FTDFluidWaterArmOutputs& Outputs, double FieldCellSizeCm)
	{
		TArray<bool> SubmergedMask;
		SubmergedMask.Init(false, Outputs.GetCellCount());
		for (int32 CellY = 0; CellY < Outputs.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Outputs.Resolution; ++CellX)
			{
				const double CellWorldX = Outputs.GetCellCenterWorld(CellX, CellY).X;
				SubmergedMask[CellY * Outputs.Resolution + CellX] = CellWorldX < -3.0 * FieldCellSizeCm || CellWorldX > 4.0 * FieldCellSizeCm;
			}
		}
		return SubmergedMask;
	}

	FTDFluidSimulationConfig MakeRingConfig(int32 Resolution, float WaveSpeedCmPerSec)
	{
		FTDFluidSimulationConfig Config = MakeWaterArmConfig(Resolution, Resolution * ParityCellSizeCm);
		Config.RippleWaveSpeedCmPerSec = WaveSpeedCmPerSec;
		Config.RippleDampingPerSec = 0.0f;
		Config.RippleFlowAdvection = 1.0f;
		Config.VorticityStrength = 0.0f;
		return Config;
	}

	FTDFluidBrushStamp MakeRippleStamp(const FVector& WorldLocation, float RadiusCm, float ImpulseCm)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.WorldLocation = WorldLocation;
		Stamp.RadiusCm = RadiusCm;
		Stamp.DensityStrength = 0.0f;
		Stamp.VelocityStrength = 0.0f;
		Stamp.RippleImpulseCm = ImpulseCm;
		Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
		return Stamp;
	}

	FString GetNonWaterBaselinePath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX"), TEXT("fx4b-nonwater-baseline.json"));
	}

	bool ReadRenderTargetChecksum(UTexture* Texture, uint32& OutChecksum)
	{
		TArray<FFloat16Color> Pixels;
		if (!ReadRenderTargetPixels(Texture, Pixels))
		{
			OutChecksum = 0u;
			return false;
		}
		OutChecksum = ComputePixelChecksum(Pixels);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiNonWaterMultigridMatchesHeadBaselineTest,
	"TDGame.FluidFXRhi.Water.NonWaterMultigridMatchesHeadBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiNonWaterMultigridMatchesHeadBaselineTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.NonWaterMultigridMatchesHeadBaseline");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope PressureSolverScope(GpuPressureSolverVariableName, 0);
	FTDWaterGpuConsoleVariableScope PressureCyclesScope(GpuPressureCyclesVariableName, 1);
	const FVector BaselineAnchorLocation(310.0, -145.0, 0.0);
	constexpr int32 BaselineStepCount = 60;
	TSharedRef<FJsonObject> MeasuredObject = MakeShared<FJsonObject>();
	MeasuredObject->SetStringField(TEXT("adapter"), GRHIAdapterName);
	MeasuredObject->SetStringField(TEXT("driver"), GRHIAdapterUserDriverVersion);
	MeasuredObject->SetStringField(TEXT("driver_internal"), GRHIAdapterInternalDriverVersion);
	for (const int32 BaselineResolution : { 256, 512, 1024 })
	{
		FTDFluidGpuWaterRhiTestWorld BaselineWorld;
		ATDFluidLiveActor* LiveActor = SpawnConfiguredLiveActor(*BaselineWorld.World, BaselineAnchorLocation, MakeNonWaterConfig(BaselineResolution));
		if (!TestNotNull(*FString::Printf(TEXT("%s spawned the nonwater %d actor"), *CaseLabel, BaselineResolution), LiveActor))
		{
			return false;
		}
		for (int32 StepIndex = 0; StepIndex < BaselineStepCount; ++StepIndex)
		{
			LiveActor->LiveComponent->QueueBrushStamp(MakeOrbitSmokeStamp(BaselineAnchorLocation, StepIndex));
			BaselineWorld.StepWorld(StepSeconds60);
		}
		if (!TestTrue(*FString::Printf(TEXT("%s nonwater %d actor uses the GPU solver"), *CaseLabel, BaselineResolution), LiveActor->LiveComponent->IsUsingGpuSolver()))
		{
			return false;
		}

		uint32 DensityChecksum = 0u;
		uint32 VelocityChecksum = 0u;
		const bool bReadDensity = ReadRenderTargetChecksum(LiveActor->LiveComponent->GetDensityPressureTexture(), DensityChecksum);
		const bool bReadVelocity = ReadRenderTargetChecksum(LiveActor->LiveComponent->GetVelocityTexture(), VelocityChecksum);
		if (!TestTrue(*FString::Printf(TEXT("%s read both nonwater %d output targets"), *CaseLabel, BaselineResolution), bReadDensity && bReadVelocity))
		{
			return false;
		}
		MeasuredObject->SetNumberField(FString::Printf(TEXT("density_%d"), BaselineResolution), DensityChecksum);
		MeasuredObject->SetNumberField(FString::Printf(TEXT("velocity_%d"), BaselineResolution), VelocityChecksum);
		ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterNonWaterBaseline: resolution=%d density=0x%08X velocity=0x%08X adapter=%s"), BaselineResolution, DensityChecksum, VelocityChecksum, *GRHIAdapterName));
	}

	const FString BaselinePath = GetNonWaterBaselinePath();
	if (FPlatformMisc::GetEnvironmentVariable(CaptureBaselineEnvironmentName) == TEXT("1"))
	{
		FString OutputText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputText);
		FJsonSerializer::Serialize(MeasuredObject, Writer);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(BaselinePath), true);
		TestTrue(*(CaseLabel + TEXT(" recorded the nonwater baseline in capture mode")), FFileHelper::SaveStringToFile(OutputText, *BaselinePath));
		ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterNonWaterBaseline: captured %s"), *BaselinePath));
		return !HasAnyErrors();
	}

	FString BaselineText;
	if (!FFileHelper::LoadFileToString(BaselineText, *BaselinePath))
	{
		AddError(FString::Printf(TEXT("%s: nonwater baseline JSON is missing outside capture mode"), *CaseLabel));
		return false;
	}

	TSharedPtr<FJsonObject> BaselineObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BaselineText);
	if (!TestTrue(*(CaseLabel + TEXT(" parsed the nonwater baseline JSON")), FJsonSerializer::Deserialize(Reader, BaselineObject) && BaselineObject.IsValid()))
	{
		return false;
	}

	FString BaselineAdapter;
	FString BaselineDriver;
	BaselineObject->TryGetStringField(TEXT("adapter"), BaselineAdapter);
	BaselineObject->TryGetStringField(TEXT("driver"), BaselineDriver);
	if (BaselineAdapter != GRHIAdapterName || BaselineDriver != GRHIAdapterUserDriverVersion)
	{
		AddError(FString::Printf(TEXT("%s: the nonwater baseline was recorded on adapter %s driver %s but this run uses %s driver %s, so the outputs are not comparable"), *CaseLabel, *BaselineAdapter, *BaselineDriver, *GRHIAdapterName, *GRHIAdapterUserDriverVersion));
		return false;
	}

	for (const int32 BaselineResolution : { 256, 512, 1024 })
	{
		for (const TCHAR* OutputName : { TEXT("density"), TEXT("velocity") })
		{
			const FString FieldName = FString::Printf(TEXT("%s_%d"), OutputName, BaselineResolution);
			double ExpectedValue = -1.0;
			double MeasuredValue = -2.0;
			BaselineObject->TryGetNumberField(FieldName, ExpectedValue);
			MeasuredObject->TryGetNumberField(FieldName, MeasuredValue);
			TestTrue(*FString::Printf(TEXT("%s %s output matches the head baseline (expected %.0f measured %.0f)"), *CaseLabel, *FieldName, ExpectedValue, MeasuredValue), ExpectedValue == MeasuredValue);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiSelectsGpuSolverAndOutputsTest,
	"TDGame.FluidFXRhi.Water.SelectsGpuSolverAndOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiSelectsGpuSolverAndOutputsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.SelectsGpuSolverAndOutputs");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	FTDFluidWaterSceneSpec SceneSpec = MakeStraightRiverScene(ParityResolution, StepSeconds60);
	SceneSpec.bWaitsForTerrain = false;
	int32 SimulationResolution = 0;
	FTDFluidWaterTargetInfo Targets;
	const bool bRan = RunWaterActorArm(*this, CaseLabel, SceneSpec, ETDFluidSolverBackend::Gpu, [&SimulationResolution, &Targets](FTDFluidWaterArmContext& ArmContext) -> bool
	{
		for (int32 StepIndex = 0; StepIndex < 4; ++StepIndex)
		{
			ArmContext.Step();
		}
		SimulationResolution = ArmContext.GetComponent().GetSimulationResolution();
		Targets = ArmContext.DescribeTargets();
		return true;
	});
	if (!bRan)
	{
		return false;
	}

	TestEqual(*(CaseLabel + TEXT(" simulation resolution is the exact water tier 192")), SimulationResolution, ParityResolution);
	ExpectWaterTargets(*this, CaseLabel, Targets, ParityResolution);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiOpenBordersDischargeParity60HzTest,
	"TDGame.FluidFXRhi.Water.River.OpenBordersDischargeParity60Hz",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiOpenBordersDischargeParity60HzTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;
	return RunOpenBorderDischargeParity(*this, TEXT("TDGame.FluidFXRhi.Water.River.OpenBordersDischargeParity60Hz"), StepSeconds60, DischargeStepCount60, ParityResolution) && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiOpenBordersDischargeParity144HzTest,
	"TDGame.FluidFXRhi.Water.River.OpenBordersDischargeParity144Hz",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiOpenBordersDischargeParity144HzTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;
	return RunOpenBorderDischargeParity(*this, TEXT("TDGame.FluidFXRhi.Water.River.OpenBordersDischargeParity144Hz"), StepSeconds144, DischargeStepCount144, ParityResolution) && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiClosedBordersControlStallsTest,
	"TDGame.FluidFXRhi.Water.River.ClosedBordersControlStalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiClosedBordersControlStallsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.River.ClosedBordersControlStalls");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	const FString OpenLabel = CaseLabel + TEXT(" GPU 192 open borders");
	FTDFluidWaterRiverArm OpenArm;
	if (!RunRiverArm(*this, OpenLabel, MakeStraightRiverScene(ParityResolution, StepSeconds60), ETDFluidSolverBackend::Gpu, DischargeStepCount60, OpenArm))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, OpenLabel, OpenArm.Targets, OpenArm.Outputs, ParityResolution) || !ExpectFlowActivity(*this, OpenLabel, OpenArm.Outputs, OpenArm.Oracle))
	{
		return false;
	}

	const TArray<FTDFluidWaterSectionFlux> OpenSections = MeasureDischargeSections(OpenArm.Outputs, OpenArm.Oracle);
	ReportSections(*this, OpenLabel, OpenSections);
	for (int32 SectionIndex = 0; SectionIndex < OpenSections.Num(); ++SectionIndex)
	{
		TestTrue(*FString::Printf(TEXT("%s section %d keeps at least 70 percent of the target discharge"), *OpenLabel, DischargeSectionColumns[SectionIndex]), ComputeFluxRatio(OpenSections[SectionIndex].NetFlux, OpenSections[SectionIndex].TargetFlux) >= 0.70);
	}

	const FString ClosedLabel = CaseLabel + TEXT(" GPU 192 closed borders");
	FTDFluidWaterSceneSpec ClosedScene = MakeStraightRiverScene(ParityResolution, StepSeconds60);
	ClosedScene.Config.bOpenWaterBorders = false;
	FTDFluidWaterRiverArm ClosedArm;
	if (!RunRiverArm(*this, ClosedLabel, ClosedScene, ETDFluidSolverBackend::Gpu, DischargeStepCount60, ClosedArm))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, ClosedLabel, ClosedArm.Targets, ClosedArm.Outputs, ParityResolution))
	{
		return false;
	}

	const TArray<FTDFluidWaterSectionFlux> ClosedSections = MeasureDischargeSections(ClosedArm.Outputs, ClosedArm.Oracle);
	ReportSections(*this, ClosedLabel, ClosedSections);
	for (int32 SectionIndex = 0; SectionIndex < ClosedSections.Num(); ++SectionIndex)
	{
		TestTrue(*FString::Printf(TEXT("%s section %d stalls below 30 percent of the target discharge"), *ClosedLabel, DischargeSectionColumns[SectionIndex]), ComputeFluxRatio(ClosedSections[SectionIndex].NetFlux, ClosedSections[SectionIndex].TargetFlux) < 0.30);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiOpenBordersDischarge1024At60HzTest,
	"TDGame.FluidFXRhi.Water.River.OpenBordersDischarge1024At60Hz",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiOpenBordersDischarge1024At60HzTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;
	return RunOpenBorderDischargeParity(*this, TEXT("TDGame.FluidFXRhi.Water.River.OpenBordersDischarge1024At60Hz"), StepSeconds60, DischargeStepCount60, 1024) && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiOpenBordersDischarge1024At144HzTest,
	"TDGame.FluidFXRhi.Water.River.OpenBordersDischarge1024At144Hz",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiOpenBordersDischarge1024At144HzTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;
	return RunOpenBorderDischargeParity(*this, TEXT("TDGame.FluidFXRhi.Water.River.OpenBordersDischarge1024At144Hz"), StepSeconds144, DischargeStepCount144, 1024) && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiNarrowingAcceleratesFlowParityTest,
	"TDGame.FluidFXRhi.Water.River.NarrowingAcceleratesFlowParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiNarrowingAcceleratesFlowParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.River.NarrowingAcceleratesFlowParity");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	constexpr double WideSectionX = -900.0;
	constexpr double NarrowSectionX = 50.0;
	const FString CpuLabel = CaseLabel + TEXT(" CPU 192");
	FTDFluidWaterRiverArm CpuArm;
	if (!RunRiverArm(*this, CpuLabel, MakeNarrowingScene(ParityResolution), ETDFluidSolverBackend::Cpu, DischargeStepCount60, CpuArm))
	{
		return false;
	}

	const double CpuWideSpeed = MeasureCenterlineSpeed(CpuArm.Outputs, WideSectionX);
	const double CpuNarrowSpeed = MeasureCenterlineSpeed(CpuArm.Outputs, NarrowSectionX);
	const double CpuRatio = CpuWideSpeed > UE_DOUBLE_SMALL_NUMBER ? CpuNarrowSpeed / CpuWideSpeed : 0.0;
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterNarrowing: %s wide=%.2f narrow=%.2f ratio=%.3f"), *CpuLabel, CpuWideSpeed, CpuNarrowSpeed, CpuRatio));
	TestTrue(*(CpuLabel + TEXT(" wide section flows downstream above 150 cm/s")), CpuWideSpeed > 150.0);
	TestTrue(*(CpuLabel + TEXT(" half width section center is at least 1.6 times faster")), CpuRatio >= 1.6);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	const FString GpuLabel = CaseLabel + TEXT(" GPU 192");
	FTDFluidWaterRiverArm GpuArm;
	if (!RunRiverArm(*this, GpuLabel, MakeNarrowingScene(ParityResolution), ETDFluidSolverBackend::Gpu, DischargeStepCount60, GpuArm))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, GpuLabel, GpuArm.Targets, GpuArm.Outputs, ParityResolution) || !ExpectFlowActivity(*this, GpuLabel, GpuArm.Outputs, GpuArm.Oracle))
	{
		return false;
	}

	const double GpuWideSpeed = MeasureCenterlineSpeed(GpuArm.Outputs, WideSectionX);
	const double GpuNarrowSpeed = MeasureCenterlineSpeed(GpuArm.Outputs, NarrowSectionX);
	const double GpuRatio = GpuWideSpeed > UE_DOUBLE_SMALL_NUMBER ? GpuNarrowSpeed / GpuWideSpeed : 0.0;
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterNarrowing: %s wide=%.2f narrow=%.2f ratio=%.3f"), *GpuLabel, GpuWideSpeed, GpuNarrowSpeed, GpuRatio));
	TestTrue(*(GpuLabel + TEXT(" half width section center is at least 1.6 times faster")), GpuRatio >= 1.6);
	TestTrue(*FString::Printf(TEXT("%s speed ratio %.3f is within 10 percent of the CPU ratio %.3f"), *GpuLabel, GpuRatio, CpuRatio), FMath::Abs(GpuRatio - CpuRatio) <= 0.10 * CpuRatio);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiRockWakeParityTest,
	"TDGame.FluidFXRhi.Water.River.RockWakeParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiRockWakeParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.River.RockWakeParity");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	constexpr int32 RockStepCount = 1800;
	const FVector2D RockCenter(-200.0, 0.0);
	FTDFluidWaterSceneSpec RockScene = MakeRockRiverScene(ParityResolution, RockCenter);
	RockScene.Config.WhitewaterFromVorticity = 0.3f;

	const FString CpuLabel = CaseLabel + TEXT(" CPU 192");
	FTDFluidWaterRiverArm CpuArm;
	if (!RunRiverArm(*this, CpuLabel, RockScene, ETDFluidSolverBackend::Cpu, RockStepCount, CpuArm))
	{
		return false;
	}

	const FTDFluidWaterRockMetrics CpuMetrics = MeasureRockMetrics(CpuArm.Outputs, RockCenter, 70.0);
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterRock: %s solid=%d rock_speed=%.3f upper_vy=%.2f lower_vy=%.2f wake=%.3f upstream=%.3f"), *CpuLabel, CpuMetrics.bRockIsSolid ? 1 : 0, CpuMetrics.RockSpeed, CpuMetrics.UpperFlankLateral, CpuMetrics.LowerFlankLateral, CpuMetrics.WakeWhitewater, CpuMetrics.UpstreamWhitewater));
	TestTrue(*(CpuLabel + TEXT(" rock cell is solid")), CpuMetrics.bRockIsSolid);
	TestTrue(*(CpuLabel + TEXT(" velocity inside the rock is zero")), CpuMetrics.RockSpeed <= 0.001);
	TestTrue(*(CpuLabel + TEXT(" whitewater within 3 m downstream of the rock reaches 0.2")), CpuMetrics.WakeWhitewater >= 0.2);
	TestTrue(*(CpuLabel + TEXT(" whitewater upstream of the rock stays lower than in the wake")), CpuMetrics.UpstreamWhitewater < CpuMetrics.WakeWhitewater);
	TestTrue(*(CpuLabel + TEXT(" upstream shoulders flow apart with opposite signs of at least 3 cm/s")), CpuMetrics.UpperFlankLateral >= 3.0 && CpuMetrics.LowerFlankLateral <= -3.0);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	const FString GpuLabel = CaseLabel + TEXT(" GPU 192");
	FTDFluidWaterRiverArm GpuArm;
	if (!RunRiverArm(*this, GpuLabel, RockScene, ETDFluidSolverBackend::Gpu, RockStepCount, GpuArm))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, GpuLabel, GpuArm.Targets, GpuArm.Outputs, ParityResolution) || !ExpectFlowActivity(*this, GpuLabel, GpuArm.Outputs, GpuArm.Oracle))
	{
		return false;
	}

	const FTDFluidWaterRockMetrics GpuMetrics = MeasureRockMetrics(GpuArm.Outputs, RockCenter, 70.0);
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterRock: %s solid=%d rock_speed=%.3f upper_vy=%.2f lower_vy=%.2f wake=%.3f upstream=%.3f"), *GpuLabel, GpuMetrics.bRockIsSolid ? 1 : 0, GpuMetrics.RockSpeed, GpuMetrics.UpperFlankLateral, GpuMetrics.LowerFlankLateral, GpuMetrics.WakeWhitewater, GpuMetrics.UpstreamWhitewater));
	TestTrue(*(GpuLabel + TEXT(" whitewater within 3 m downstream of the rock reaches 0.2")), GpuMetrics.WakeWhitewater >= 0.2);
	TestTrue(*FString::Printf(TEXT("%s wake whitewater %.3f is within 25 percent of the CPU %.3f"), *GpuLabel, GpuMetrics.WakeWhitewater, CpuMetrics.WakeWhitewater), FMath::Abs(GpuMetrics.WakeWhitewater - CpuMetrics.WakeWhitewater) <= 0.25 * CpuMetrics.WakeWhitewater);
	TestTrue(*(GpuLabel + TEXT(" upstream shoulders flow apart with opposite signs of at least 3 cm/s")), GpuMetrics.UpperFlankLateral >= 3.0 && GpuMetrics.LowerFlankLateral <= -3.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiFoamBlobDriftParityTest,
	"TDGame.FluidFXRhi.Water.FlowGuide.FoamBlobDriftParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiFoamBlobDriftParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.FlowGuide.FoamBlobDriftParity");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	static constexpr int32 DriftStepCount = 60;
	constexpr double FoamSpeedCmPerSec = 200.0;
	const double ExpectedTravelCm = FoamSpeedCmPerSec * DriftStepCount * StepSeconds60;
	FTDFluidWaterSceneSpec FoamScene = MakeRiverScene(ParityResolution, ParityExtentCm, StepSeconds60, MakeStraightRiver(-2700.0, 2700.0, 900.0f, 200.0f));
	FoamScene.Config.WhitewaterFromVorticity = 0.0f;
	FoamScene.Config.RapidsWhitewaterPerSec = 0.0f;

	struct FTDFluidWaterFoamArm
	{
		double StartMass = 0.0;
		double EndMass = 0.0;
		FVector2D Drift = FVector2D::ZeroVector;
		FTDFluidWaterArmOutputs Outputs;
		FTDFluidWaterGuideOracle Oracle;
		FTDFluidWaterTargetInfo Targets;
	};

	const auto RunFoamArm = [this, &FoamScene](const FString& ArmLabel, ETDFluidSolverBackend Backend, FTDFluidWaterFoamArm& OutArm)
	{
		return RunWaterActorArm(*this, ArmLabel, FoamScene, Backend, [&OutArm](FTDFluidWaterArmContext& ArmContext) -> bool
		{
			ArmContext.Step();
			FTDFluidBrushStamp FoamStamp;
			FoamStamp.WorldLocation = FVector(-300.0, 0.0, 0.0);
			FoamStamp.RadiusCm = 90.0f;
			FoamStamp.DensityStrength = 1.0f;
			FoamStamp.VelocityStrength = 0.0f;
			FoamStamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
			ArmContext.QueueStamp(FoamStamp);
			ArmContext.Step();
			const FVector2D StartCentroid = ComputeDensityCentroid(ArmContext.Capture(), OutArm.StartMass);
			for (int32 StepIndex = 0; StepIndex < DriftStepCount; ++StepIndex)
			{
				ArmContext.Step();
			}
			OutArm.Outputs = ArmContext.Capture();
			OutArm.Drift = ComputeDensityCentroid(OutArm.Outputs, OutArm.EndMass) - StartCentroid;
			OutArm.Oracle = ArmContext.BuildOracle();
			OutArm.Targets = ArmContext.DescribeTargets();
			return true;
		});
	};

	const FString CpuLabel = CaseLabel + TEXT(" CPU 192");
	FTDFluidWaterFoamArm CpuArm;
	if (!RunFoamArm(CpuLabel, ETDFluidSolverBackend::Cpu, CpuArm))
	{
		return false;
	}

	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterFoam: %s drift_x=%.1f drift_y=%.1f mass=%.2f->%.2f expected=%.1f"), *CpuLabel, CpuArm.Drift.X, CpuArm.Drift.Y, CpuArm.StartMass, CpuArm.EndMass, ExpectedTravelCm));
	TestTrue(*(CpuLabel + TEXT(" stamped foam blob has mass")), CpuArm.StartMass > 1.0);
	TestTrue(*(CpuLabel + TEXT(" foam drifts downstream at least half the guide travel")), CpuArm.Drift.X >= 0.5 * ExpectedTravelCm);
	TestTrue(*(CpuLabel + TEXT(" foam does not outrun the guide flow")), CpuArm.Drift.X <= 1.5 * ExpectedTravelCm);
	TestTrue(*(CpuLabel + TEXT(" foam stays on the guide line")), FMath::Abs(CpuArm.Drift.Y) < 40.0);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	const FString GpuLabel = CaseLabel + TEXT(" GPU 192");
	FTDFluidWaterFoamArm GpuArm;
	if (!RunFoamArm(GpuLabel, ETDFluidSolverBackend::Gpu, GpuArm))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, GpuLabel, GpuArm.Targets, GpuArm.Outputs, ParityResolution) || !ExpectFlowActivity(*this, GpuLabel, GpuArm.Outputs, GpuArm.Oracle))
	{
		return false;
	}

	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterFoam: %s drift_x=%.1f drift_y=%.1f mass=%.2f->%.2f expected=%.1f"), *GpuLabel, GpuArm.Drift.X, GpuArm.Drift.Y, GpuArm.StartMass, GpuArm.EndMass, ExpectedTravelCm));
	TestTrue(*(GpuLabel + TEXT(" stamped foam blob has mass")), GpuArm.StartMass > 1.0);
	TestTrue(*(GpuLabel + TEXT(" foam drifts downstream at least half the guide travel")), GpuArm.Drift.X >= 0.5 * ExpectedTravelCm);
	TestTrue(*(GpuLabel + TEXT(" foam does not outrun the guide flow")), GpuArm.Drift.X <= 1.5 * ExpectedTravelCm);
	TestTrue(*(GpuLabel + TEXT(" foam stays on the guide line")), FMath::Abs(GpuArm.Drift.Y) < 40.0);
	TestTrue(*FString::Printf(TEXT("%s drift %.1f cm is within 15 percent of the expected travel from the CPU drift %.1f cm"), *GpuLabel, GpuArm.Drift.X, CpuArm.Drift.X), FMath::Abs(GpuArm.Drift.X - CpuArm.Drift.X) <= 0.15 * ExpectedTravelCm);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiRingRadiusParityTest,
	"TDGame.FluidFXRhi.Water.Ripple.RingRadiusParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiRingRadiusParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Ripple.RingRadiusParity");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	constexpr float WaveSpeedCmPerSec = 120.0f;
	constexpr float ImpulseCm = 6.0f;
	constexpr int32 RingStepCount = 200;
	constexpr int32 CenterCell = ParityResolution / 2;
	const double ExpectedRadiusCells = WaveSpeedCmPerSec * RingStepCount * StepSeconds60 / ParityCellSizeCm;

	FTDFluidWaterArmOutputs GpuFirstOutputs;
	FTDFluidWaterArmOutputs CpuRingOutputs;
	FTDFluidWaterArmOutputs GpuRingOutputs;
	FTDFluidWaterTargetInfo GpuTargets;
	FTDFluidWaterPairResult PairResult;
	const bool bRan = RunLiveActorPair(*this, CaseLabel, MakeRingConfig(ParityResolution, WaveSpeedCmPerSec), WaterGpuScope.IsValid(), PairResult, [&](FTDFluidWaterPairContext& PairContext) -> bool
	{
		PairContext.QueueStamp(MakeRippleStamp(PairContext.GetCellCenterWorld(CenterCell, CenterCell), 40.0f, ImpulseCm));
		PairContext.Step(StepSeconds60);
		GpuFirstOutputs = PairContext.CaptureGpu();
		for (int32 StepIndex = 1; StepIndex < RingStepCount; ++StepIndex)
		{
			PairContext.Step(StepSeconds60);
		}
		CpuRingOutputs = PairContext.CaptureCpu();
		GpuRingOutputs = PairContext.CaptureGpu();
		GpuTargets = PairContext.DescribeGpuTargets();
		return true;
	});
	if (!bRan)
	{
		return false;
	}

	const FTDFluidWaterRing CpuRing = MeasureRingRadius(DecodeRippleHeights(CpuRingOutputs, DefaultRippleReferenceCm), CpuRingOutputs.Resolution, CenterCell, CenterCell);
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterRing: %s CPU radius_cells=%.3f expected=%.3f peak_cm=%.4f"), *CaseLabel, CpuRing.RadiusCells, ExpectedRadiusCells, CpuRing.PeakAmplitudeCm));
	TestTrue(*(CaseLabel + TEXT(" CPU ring leading lobe sits at wave speed times time within 1.5 cells")), FMath::Abs(CpuRing.RadiusCells - ExpectedRadiusCells) <= 1.5);
	TestTrue(*(CaseLabel + TEXT(" CPU ring still carries amplitude")), CpuRing.PeakAmplitudeCm > 0.01);
	if (!ExpectPairGpuArm(*this, CaseLabel, WaterGpuScope, PairResult))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, CaseLabel + TEXT(" GPU 192"), GpuTargets, GpuRingOutputs, ParityResolution) || !ExpectRippleActivity(*this, CaseLabel + TEXT(" GPU 192"), GpuFirstOutputs, ImpulseCm, DefaultRippleReferenceCm))
	{
		return false;
	}

	const FTDFluidWaterRing GpuRing = MeasureRingRadius(DecodeRippleHeights(GpuRingOutputs, DefaultRippleReferenceCm), GpuRingOutputs.Resolution, CenterCell, CenterCell);
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterRing: %s GPU radius_cells=%.3f expected=%.3f peak_cm=%.4f"), *CaseLabel, GpuRing.RadiusCells, ExpectedRadiusCells, GpuRing.PeakAmplitudeCm));
	TestTrue(*(CaseLabel + TEXT(" GPU ring leading lobe sits at wave speed times time within 1.5 cells")), FMath::Abs(GpuRing.RadiusCells - ExpectedRadiusCells) <= 1.5);
	TestTrue(*(CaseLabel + TEXT(" GPU ring radius is within one cell of the CPU ring")), FMath::Abs(GpuRing.RadiusCells - CpuRing.RadiusCells) <= 1.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiBorderAbsorbsParityTest,
	"TDGame.FluidFXRhi.Water.Ripple.BorderAbsorbsParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiBorderAbsorbsParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Ripple.BorderAbsorbsParity");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	constexpr int32 AbsorbResolution = 128;
	constexpr int32 CenterCell = AbsorbResolution / 2;
	constexpr float ImpulseCm = -6.0f;
	constexpr int32 ReferenceStepCount = 90;
	constexpr int32 AbsorbStepCount = 840;

	FTDFluidWaterArmOutputs GpuFirstOutputs;
	FTDFluidWaterTargetInfo GpuTargets;
	FTDFluidWaterArmOutputs GpuFinalOutputs;
	double CpuInitialEnergy = 0.0;
	double CpuFinalEnergy = 0.0;
	double GpuInitialEnergy = 0.0;
	double GpuFinalEnergy = 0.0;
	FTDFluidWaterPairResult PairResult;
	const bool bRan = RunLiveActorPair(*this, CaseLabel, MakeRingConfig(AbsorbResolution, 200.0f), WaterGpuScope.IsValid(), PairResult, [&](FTDFluidWaterPairContext& PairContext) -> bool
	{
		PairContext.QueueStamp(MakeRippleStamp(PairContext.GetCellCenterWorld(CenterCell, CenterCell), 40.0f, ImpulseCm));
		PairContext.Step(StepSeconds60);
		GpuFirstOutputs = PairContext.CaptureGpu();
		for (int32 StepIndex = 1; StepIndex < ReferenceStepCount; ++StepIndex)
		{
			PairContext.Step(StepSeconds60);
		}
		CpuInitialEnergy = MeasureRippleEnergy(PairContext.CaptureCpu(), DefaultRippleReferenceCm);
		GpuInitialEnergy = MeasureRippleEnergy(PairContext.CaptureGpu(), DefaultRippleReferenceCm);
		for (int32 StepIndex = 0; StepIndex < AbsorbStepCount; ++StepIndex)
		{
			PairContext.Step(StepSeconds60);
		}
		CpuFinalEnergy = MeasureRippleEnergy(PairContext.CaptureCpu(), DefaultRippleReferenceCm);
		GpuFinalOutputs = PairContext.CaptureGpu();
		GpuFinalEnergy = MeasureRippleEnergy(GpuFinalOutputs, DefaultRippleReferenceCm);
		GpuTargets = PairContext.DescribeGpuTargets();
		return true;
	});
	if (!bRan)
	{
		return false;
	}

	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterAbsorb: %s CPU initial=%.4f final=%.6f"), *CaseLabel, CpuInitialEnergy, CpuFinalEnergy));
	TestTrue(*(CaseLabel + TEXT(" CPU expanding ring carries energy before it reaches the border")), CpuInitialEnergy > 0.01);
	TestTrue(*(CaseLabel + TEXT(" CPU interior energy after the wave crossed the border is under 10 percent")), CpuFinalEnergy < 0.10 * CpuInitialEnergy);
	if (!ExpectPairGpuArm(*this, CaseLabel, WaterGpuScope, PairResult))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, CaseLabel + TEXT(" GPU 128"), GpuTargets, GpuFinalOutputs, AbsorbResolution) || !ExpectRippleActivity(*this, CaseLabel + TEXT(" GPU 128"), GpuFirstOutputs, ImpulseCm, DefaultRippleReferenceCm))
	{
		return false;
	}

	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterAbsorb: %s GPU initial=%.4f final=%.6f"), *CaseLabel, GpuInitialEnergy, GpuFinalEnergy));
	TestTrue(*(CaseLabel + TEXT(" GPU initial energy is at least half the CPU initial energy")), GpuInitialEnergy >= 0.5 * CpuInitialEnergy);
	TestTrue(*(CaseLabel + TEXT(" GPU interior energy after the wave crossed the border is under 10 percent")), GpuFinalEnergy < 0.10 * GpuInitialEnergy);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiAdvectionKeepsAmplitudeParityTest,
	"TDGame.FluidFXRhi.Water.Ripple.AdvectionKeepsAmplitudeParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiAdvectionKeepsAmplitudeParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Ripple.AdvectionKeepsAmplitudeParity");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	static constexpr int32 AdvectionResolution = 64;
	static constexpr float AmplitudeCm = 4.0f;
	static constexpr double WavelengthCells = 8.0;
	static constexpr int32 StampsPerFrame = 512;
	static constexpr int32 AdvectionStepCount = 60;

	FTDFluidWaterSceneSpec AdvectionScene;
	AdvectionScene.Config = MakeWaterArmConfig(AdvectionResolution, AdvectionResolution * ParityCellSizeCm);
	AdvectionScene.Config.BorderFadeRatio = 0.0f;
	AdvectionScene.Config.VorticityStrength = 0.0f;
	AdvectionScene.Config.RippleWaveSpeedCmPerSec = 0.5f;
	AdvectionScene.Config.RippleDampingPerSec = 0.0f;
	AdvectionScene.Config.RippleFlowAdvection = 1.0f;
	AdvectionScene.bSamplesTerrain = false;
	AdvectionScene.bWaitsForTerrain = false;

	struct FTDFluidWaterAdvectionArm
	{
		FTDFluidWaterArmOutputs FirstStampOutputs;
		FTDFluidWaterArmOutputs FinalOutputs;
		FTDFluidWaterTargetInfo Targets;
		double InitialAmplitude = 0.0;
		double FinalAmplitude = 0.0;
		double CenterVelocityX = 0.0;
	};

	const auto RunAdvectionArm = [this, &AdvectionScene](const FString& ArmLabel, ETDFluidSolverBackend Backend, FTDFluidWaterAdvectionArm& OutArm)
	{
		return RunWaterActorArm(*this, ArmLabel, AdvectionScene, Backend, [this, &ArmLabel, &OutArm](FTDFluidWaterArmContext& ArmContext) -> bool
		{
			TArray<FTDFluidBrushStamp> SinusoidStamps;
			for (int32 CellY = 0; CellY < AdvectionResolution; ++CellY)
			{
				for (int32 CellX = 0; CellX < AdvectionResolution; ++CellX)
				{
					const FVector2D CellWorld = ArmContext.GetComponent().CellToPlaneCenter(CellX, CellY);
					SinusoidStamps.Add(MakeRippleStamp(FVector(CellWorld.X, CellWorld.Y, 0.0), 5.0f, static_cast<float>(-AmplitudeCm * FMath::Sin(UE_DOUBLE_TWO_PI * CellX / WavelengthCells))));
				}
			}

			for (int32 FirstStamp = 0; FirstStamp < SinusoidStamps.Num(); FirstStamp += StampsPerFrame)
			{
				for (int32 StampIndex = FirstStamp; StampIndex < FMath::Min(FirstStamp + StampsPerFrame, SinusoidStamps.Num()); ++StampIndex)
				{
					ArmContext.QueueStamp(SinusoidStamps[StampIndex]);
				}
				ArmContext.Step();
				if (FirstStamp == 0)
				{
					OutArm.FirstStampOutputs = ArmContext.Capture();
				}
			}

			OutArm.InitialAmplitude = MeasureAdvectionAmplitude(ArmContext.Capture(), DefaultRippleReferenceCm);
			if (!ArmContext.AddRiver(MakeStraightRiver(-1500.0, 1500.0, 5000.0f, 250.0f)))
			{
				AddError(FString::Printf(TEXT("%s: the advection river could not be spawned (harness defect)"), *ArmLabel));
				return false;
			}
			for (int32 StepIndex = 0; StepIndex < AdvectionStepCount; ++StepIndex)
			{
				ArmContext.Step();
			}
			OutArm.FinalOutputs = ArmContext.Capture();
			OutArm.FinalAmplitude = MeasureAdvectionAmplitude(OutArm.FinalOutputs, DefaultRippleReferenceCm);
			const int32 CenterIndex = (AdvectionResolution / 2) * AdvectionResolution + AdvectionResolution / 2;
			OutArm.CenterVelocityX = OutArm.FinalOutputs.HasAllPixels() ? OutArm.FinalOutputs.DecodeVelocity(CenterIndex).X : 0.0;
			OutArm.Targets = ArmContext.DescribeTargets();
			return true;
		});
	};

	const FString CpuLabel = CaseLabel + TEXT(" CPU 64");
	FTDFluidWaterAdvectionArm CpuArm;
	if (!RunAdvectionArm(CpuLabel, ETDFluidSolverBackend::Cpu, CpuArm))
	{
		return false;
	}

	const double CpuRatio = CpuArm.InitialAmplitude > UE_DOUBLE_SMALL_NUMBER ? CpuArm.FinalAmplitude / CpuArm.InitialAmplitude : 0.0;
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterAdvection: %s initial=%.4f final=%.4f ratio=%.4f flow_vx=%.2f"), *CpuLabel, CpuArm.InitialAmplitude, CpuArm.FinalAmplitude, CpuRatio, CpuArm.CenterVelocityX));
	TestTrue(*(CpuLabel + TEXT(" sinusoid was written after the eighth stamp frame")), CpuArm.InitialAmplitude > 0.9 * AmplitudeCm);
	TestTrue(*(CpuLabel + TEXT(" flow advects at about 0.2 cells per step")), CpuArm.CenterVelocityX > 200.0 && CpuArm.CenterVelocityX < 270.0);
	TestTrue(*(CpuLabel + TEXT(" 8 cell wave keeps at least 70 percent amplitude after 60 advection steps")), CpuRatio >= 0.7);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	const FString GpuLabel = CaseLabel + TEXT(" GPU 64");
	FTDFluidWaterAdvectionArm GpuArm;
	if (!RunAdvectionArm(GpuLabel, ETDFluidSolverBackend::Gpu, GpuArm))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, GpuLabel, GpuArm.Targets, GpuArm.FinalOutputs, AdvectionResolution) || !ExpectRippleActivity(*this, GpuLabel, GpuArm.FirstStampOutputs, AmplitudeCm, DefaultRippleReferenceCm))
	{
		return false;
	}

	const double GpuRatio = GpuArm.InitialAmplitude > UE_DOUBLE_SMALL_NUMBER ? GpuArm.FinalAmplitude / GpuArm.InitialAmplitude : 0.0;
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterAdvection: %s initial=%.4f final=%.4f ratio=%.4f flow_vx=%.2f"), *GpuLabel, GpuArm.InitialAmplitude, GpuArm.FinalAmplitude, GpuRatio, GpuArm.CenterVelocityX));
	TestTrue(*(GpuLabel + TEXT(" 8 cell wave keeps at least 70 percent amplitude after 60 advection steps")), GpuRatio >= 0.7);
	TestTrue(*FString::Printf(TEXT("%s amplitude ratio %.4f is within 0.05 of the CPU ratio %.4f"), *GpuLabel, GpuRatio, CpuRatio), FMath::Abs(GpuRatio - CpuRatio) <= 0.05);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiBrushDoesNotWriteDisplacementTest,
	"TDGame.FluidFXRhi.Water.Ripple.BrushDoesNotWriteDisplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiBrushDoesNotWriteDisplacementTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Ripple.BrushDoesNotWriteDisplacement");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	constexpr int32 CenterCell = ParityResolution / 2;
	static constexpr float ExpectedRippleEncoding = 0.3125f;
	FTDFluidWaterArmOutputs CpuOutputs;
	FTDFluidWaterArmOutputs GpuOutputs;
	FTDFluidWaterTargetInfo GpuTargets;
	FTDFluidWaterPairResult PairResult;
	const bool bRan = RunLiveActorPair(*this, CaseLabel, MakeWaterArmConfig(ParityResolution, ParityExtentCm), WaterGpuScope.IsValid(), PairResult, [&](FTDFluidWaterPairContext& PairContext) -> bool
	{
		FTDFluidBrushStamp Stamp = MakeRippleStamp(PairContext.GetCellCenterWorld(CenterCell, CenterCell), 80.0f, 3.0f);
		Stamp.DensityStrength = 1.0f;
		PairContext.QueueStamp(Stamp);
		PairContext.Step(StepSeconds60);
		CpuOutputs = PairContext.CaptureCpu();
		GpuOutputs = PairContext.CaptureGpu();
		GpuTargets = PairContext.DescribeGpuTargets();
		return true;
	});
	if (!bRan)
	{
		return false;
	}

	const int32 CenterIndex = CenterCell * ParityResolution + CenterCell;
	const auto ExpectStampEncoding = [this](const FString& ArmLabel, const FTDFluidWaterArmOutputs& Outputs)
	{
		if (!TestTrue(*(ArmLabel + TEXT(" captured every output pixel")), Outputs.HasAllPixels()))
		{
			return;
		}
		const int32 StampCellIndex = (Outputs.Resolution / 2) * Outputs.Resolution + Outputs.Resolution / 2;
		const float PressureEncoding = Outputs.DensityPixels[StampCellIndex].G.GetFloat();
		const float RippleEncoding = Outputs.SurfacePixels[StampCellIndex].B.GetFloat();
		TestTrue(*FString::Printf(TEXT("%s keeps the pressure channel at 0.5 (got %.5f)"), *ArmLabel, PressureEncoding), FMath::IsNearlyEqual(PressureEncoding, 0.5f, EncodingTolerance));
		TestTrue(*FString::Printf(TEXT("%s encodes the -3 cm ripple as 0.3125 (got %.5f)"), *ArmLabel, RippleEncoding), FMath::IsNearlyEqual(RippleEncoding, ExpectedRippleEncoding, EncodingTolerance));
	};

	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterBrush: %s center_index=%d"), *CaseLabel, CenterIndex));
	ExpectStampEncoding(CaseLabel + TEXT(" CPU 192"), CpuOutputs);
	if (!ExpectPairGpuArm(*this, CaseLabel, WaterGpuScope, PairResult))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, CaseLabel + TEXT(" GPU 192"), GpuTargets, GpuOutputs, ParityResolution))
	{
		return false;
	}
	ExpectStampEncoding(CaseLabel + TEXT(" GPU 192"), GpuOutputs);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiFlatSurfaceOverSlopeStaysStillTest,
	"TDGame.FluidFXRhi.Water.WaterLayer.FlatSurfaceOverSlopeStaysStill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiFlatSurfaceOverSlopeStaysStillTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.WaterLayer.FlatSurfaceOverSlopeStaysStill");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	constexpr int32 StillStepCount = 300;
	const FTDFluidWaterSceneSpec TiltedScene = MakeTiltedBedScene();
	const FString CpuLabel = CaseLabel + TEXT(" CPU 192");
	FTDFluidWaterRiverArm CpuArm;
	if (!RunRiverArm(*this, CpuLabel, TiltedScene, ETDFluidSolverBackend::Cpu, StillStepCount, CpuArm))
	{
		return false;
	}

	const int32 CenterIndex = (ParityResolution / 2) * ParityResolution + ParityResolution / 2;
	const double CpuAverageSpeed = MeasureAverageSpeed(CpuArm.Outputs);
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterStill: %s average_speed_cm_per_sec=%.5f"), *CpuLabel, CpuAverageSpeed));
	if (!TestTrue(*(CpuLabel + TEXT(" captured every packed output pixel")), CpuArm.Outputs.HasAllPixels()))
	{
		return false;
	}
	TestTrue(*(CpuLabel + TEXT(" center cell is water deeper than 1 m over the sloped bed")), CpuArm.Outputs.GetDepthEncoding(CenterIndex) * TiltedScene.Config.WaterDepthReferenceCm > 100.0f && !CpuArm.Outputs.IsSolid(CenterIndex));
	TestTrue(*(CpuLabel + TEXT(" flat water surface over a sloped bed stays still (average speed under 1 cm/s)")), CpuAverageSpeed < 1.0);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	const FString GpuLabel = CaseLabel + TEXT(" GPU 192");
	FTDFluidWaterRiverArm GpuArm;
	if (!RunRiverArm(*this, GpuLabel, TiltedScene, ETDFluidSolverBackend::Gpu, StillStepCount, GpuArm))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, GpuLabel, GpuArm.Targets, GpuArm.Outputs, ParityResolution) || !ExpectStillActivity(*this, GpuLabel, GpuArm.Outputs, MakeWetCellMask(CpuArm.Outputs)))
	{
		return false;
	}

	const double GpuAverageSpeed = MeasureAverageSpeed(GpuArm.Outputs);
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterStill: %s average_speed_cm_per_sec=%.5f"), *GpuLabel, GpuAverageSpeed));
	TestTrue(*(GpuLabel + TEXT(" flat water surface over a sloped bed stays still (average speed under 1 cm/s)")), GpuAverageSpeed < 1.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiStaysFiniteWithIrregularStepsTest,
	"TDGame.FluidFXRhi.Water.WaterLayer.StaysFiniteWithIrregularSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiStaysFiniteWithIrregularStepsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.WaterLayer.StaysFiniteWithIrregularSteps");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	static constexpr float IrregularStepSecondsCycle[] = { 1.0f / 60.0f, 1.0f / 30.0f, 0.1f, 0.15f, 1.0f / 90.0f };
	constexpr int32 IrregularStepCount = 600;
	constexpr int32 LargeStepCount = 300;
	constexpr double MaximumExpectedSpeedCmPerSec = 1200.5;

	struct FTDFluidWaterIrregularArm
	{
		FTDFluidWaterArmOutputs Outputs;
		FTDFluidWaterGuideOracle Oracle;
		FTDFluidWaterTargetInfo Targets;
	};

	const auto RunIrregularArm = [this](const FString& ArmLabel, int32 Resolution, double ExtentCm, ETDFluidSolverBackend Backend, bool bUsesIrregularCycle, int32 StepCount, bool bWaitsForTerrain, FTDFluidWaterIrregularArm& OutArm)
	{
		FTDFluidWaterSceneSpec IrregularScene = MakeScenarioRiverScene(Resolution, ExtentCm);
		IrregularScene.bWaitsForTerrain = bWaitsForTerrain;
		return RunWaterActorArm(*this, ArmLabel, IrregularScene, Backend, [&OutArm, bUsesIrregularCycle, StepCount](FTDFluidWaterArmContext& ArmContext) -> bool
		{
			for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
			{
				if (bUsesIrregularCycle)
				{
					ArmContext.Anchor->SetActorLocation(FVector(StepIndex * 3.0, 0.0, 0.0));
				}
				for (const FTDFluidBrushStamp& Stamp : MakeOrbitWaterStamps(ArmContext.GetComponent().GetSimulationCenterWorld(), StepIndex))
				{
					ArmContext.QueueStamp(Stamp);
				}
				ArmContext.StepFor(bUsesIrregularCycle ? IrregularStepSecondsCycle[StepIndex % UE_ARRAY_COUNT(IrregularStepSecondsCycle)] : LargeStepSeconds);
			}
			OutArm.Outputs = ArmContext.Capture();
			OutArm.Oracle = ArmContext.BuildOracle();
			OutArm.Targets = ArmContext.DescribeTargets();
			return true;
		});
	};

	const auto ReportHealth = [this](const FString& ArmLabel, const FTDFluidWaterOutputHealth& Health)
	{
		ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterFinite: %s non_finite=%d out_of_range=%d max_speed=%.2f"), *ArmLabel, Health.NonFiniteChannelCount, Health.OutOfRangeChannelCount, Health.MaximumSpeedCmPerSec));
	};

	const FString CpuLabel = CaseLabel + TEXT(" CPU 192");
	FTDFluidWaterIrregularArm CpuArm;
	if (!RunIrregularArm(CpuLabel, ParityResolution, ParityExtentCm, ETDFluidSolverBackend::Cpu, true, IrregularStepCount, true, CpuArm))
	{
		return false;
	}

	const FTDFluidWaterOutputHealth CpuHealth = MeasureOutputHealth(CpuArm.Outputs);
	ReportHealth(CpuLabel, CpuHealth);
	TestTrue(*(CpuLabel + TEXT(" captured every packed output pixel")), CpuArm.Outputs.HasAllPixels());
	TestEqual(*(CpuLabel + TEXT(" output channels with non-finite values after irregular steps")), CpuHealth.NonFiniteChannelCount, 0);
	TestTrue(*(CpuLabel + TEXT(" decoded speed stays within the solver speed limit")), CpuHealth.MaximumSpeedCmPerSec <= MaximumExpectedSpeedCmPerSec);
	TestTrue(*(CpuLabel + TEXT(" scenario still carries flow")), CpuHealth.MaximumSpeedCmPerSec > 10.0);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	const FString GpuLabel = CaseLabel + TEXT(" GPU 192");
	FTDFluidWaterIrregularArm GpuArm;
	if (!RunIrregularArm(GpuLabel, ParityResolution, ParityExtentCm, ETDFluidSolverBackend::Gpu, true, IrregularStepCount, true, GpuArm))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, GpuLabel, GpuArm.Targets, GpuArm.Outputs, ParityResolution) || !ExpectFlowActivity(*this, GpuLabel, GpuArm.Outputs, GpuArm.Oracle))
	{
		return false;
	}

	const FTDFluidWaterOutputHealth GpuHealth = MeasureOutputHealth(GpuArm.Outputs);
	ReportHealth(GpuLabel, GpuHealth);
	TestEqual(*(GpuLabel + TEXT(" output channels with non-finite values after irregular steps")), GpuHealth.NonFiniteChannelCount, 0);
	TestTrue(*(GpuLabel + TEXT(" decoded speed stays within the solver speed limit")), GpuHealth.MaximumSpeedCmPerSec <= MaximumExpectedSpeedCmPerSec);

	for (const double LargeExtentCm : { 3600.0, 1000.0, 1100.0 })
	{
		const FString LargeLabel = FString::Printf(TEXT("%s GPU 1024 window %.0f cm dt 0.1"), *CaseLabel, LargeExtentCm);
		FTDFluidWaterIrregularArm LargeArm;
		if (!RunIrregularArm(LargeLabel, 1024, LargeExtentCm, ETDFluidSolverBackend::Gpu, false, LargeStepCount, true, LargeArm))
		{
			return false;
		}
		if (!ExpectGpuArmOutputs(*this, LargeLabel, LargeArm.Targets, LargeArm.Outputs, 1024) || !ExpectFlowActivity(*this, LargeLabel, LargeArm.Outputs, LargeArm.Oracle))
		{
			return false;
		}

		const FTDFluidWaterOutputHealth LargeHealth = MeasureOutputHealth(LargeArm.Outputs);
		ReportHealth(LargeLabel, LargeHealth);
		TestEqual(*(LargeLabel + TEXT(" output channels with non-finite values")), LargeHealth.NonFiniteChannelCount, 0);
		TestEqual(*(LargeLabel + TEXT(" output channels outside the encoded range")), LargeHealth.OutOfRangeChannelCount, 0);
	}

	const FString InformationLabel = CaseLabel + TEXT(" GPU 2048 window 3600 cm dt 0.1 (information)");
	FTDFluidWaterIrregularArm InformationArm;
	if (RunIrregularArm(InformationLabel, 2048, ParityExtentCm, ETDFluidSolverBackend::Gpu, false, 60, false, InformationArm))
	{
		ReportHealth(InformationLabel, MeasureOutputHealth(InformationArm.Outputs));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiDryCellsAreSolidTest,
	"TDGame.FluidFXRhi.Water.WaterLayer.DryCellsAreSolid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiDryCellsAreSolidTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.WaterLayer.DryCellsAreSolid");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	const FString CpuLabel = CaseLabel + TEXT(" CPU 192");
	FTDFluidWaterDryBankArm CpuArm;
	if (!RunDryBankArm(*this, CpuLabel, ETDFluidSolverBackend::Cpu, CpuArm))
	{
		return false;
	}
	if (!TestTrue(*(CpuLabel + TEXT(" captured every packed output pixel")), CpuArm.FinalOutputs.HasAllPixels()))
	{
		return false;
	}
	ExpectDryBankCells(*this, CpuLabel, CpuArm);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	const FString GpuLabel = CaseLabel + TEXT(" GPU 192");
	FTDFluidWaterDryBankArm GpuArm;
	if (!RunDryBankArm(*this, GpuLabel, ETDFluidSolverBackend::Gpu, GpuArm))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, GpuLabel, GpuArm.Targets, GpuArm.FinalOutputs, ParityResolution) || !ExpectStillActivity(*this, GpuLabel, GpuArm.FinalOutputs, MakeWetCellMask(CpuArm.FinalOutputs)))
	{
		return false;
	}
	ExpectDryBankCells(*this, GpuLabel, GpuArm);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiSurfacePackingParityTest,
	"TDGame.FluidFXRhi.Water.Output.SurfacePackingParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiSurfacePackingParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Output.SurfacePackingParity");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	const auto CountNeutralRippleAndWetness = [](const FTDFluidWaterArmOutputs& Outputs, int32& OutRippleMismatches, int32& OutWetnessMismatches)
	{
		OutRippleMismatches = 0;
		OutWetnessMismatches = 0;
		for (const FFloat16Color& Pixel : Outputs.SurfacePixels)
		{
			OutRippleMismatches += FMath::IsNearlyEqual(Pixel.B.GetFloat(), 0.5f, EncodingTolerance) ? 0 : 1;
			OutWetnessMismatches += FMath::IsNearlyEqual(Pixel.G.GetFloat(), 0.0f, EncodingTolerance) ? 0 : 1;
		}
	};

	const FString CpuLabel = CaseLabel + TEXT(" CPU 192");
	FTDFluidWaterDryBankArm CpuArm;
	if (!RunDryBankArm(*this, CpuLabel, ETDFluidSolverBackend::Cpu, CpuArm))
	{
		return false;
	}
	if (!TestTrue(*(CpuLabel + TEXT(" captured every packed output pixel")), CpuArm.FinalOutputs.HasAllPixels()))
	{
		return false;
	}

	int32 CpuRippleMismatches = 0;
	int32 CpuWetnessMismatches = 0;
	CountNeutralRippleAndWetness(CpuArm.FinalOutputs, CpuRippleMismatches, CpuWetnessMismatches);
	TestEqual(*(CpuLabel + TEXT(" water surface cells whose ripple channel is not 0.5")), CpuRippleMismatches, 0);
	TestEqual(*(CpuLabel + TEXT(" water surface cells whose wetness channel is not 0")), CpuWetnessMismatches, 0);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	const FString GpuLabel = CaseLabel + TEXT(" GPU 192");
	FTDFluidWaterDryBankArm GpuArm;
	if (!RunDryBankArm(*this, GpuLabel, ETDFluidSolverBackend::Gpu, GpuArm))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, GpuLabel, GpuArm.Targets, GpuArm.FinalOutputs, ParityResolution) || !ExpectStillActivity(*this, GpuLabel, GpuArm.FinalOutputs, MakeWetCellMask(CpuArm.FinalOutputs)))
	{
		return false;
	}

	float MaximumDepthDifference = 0.0f;
	float MaximumTerrainDifference = 0.0f;
	int32 AlphaMismatchCount = 0;
	const FTDFluidWaterArmOutputs& CpuOutputs = CpuArm.FinalOutputs;
	const FTDFluidWaterArmOutputs& GpuOutputs = GpuArm.FinalOutputs;
	for (int32 CellIndex = 0; CellIndex < CpuOutputs.GetCellCount(); ++CellIndex)
	{
		MaximumDepthDifference = FMath::Max(MaximumDepthDifference, FMath::Abs(GpuOutputs.SurfacePixels[CellIndex].R.GetFloat() - CpuOutputs.SurfacePixels[CellIndex].R.GetFloat()));
		MaximumTerrainDifference = FMath::Max(MaximumTerrainDifference, FMath::Abs(GpuOutputs.SurfacePixels[CellIndex].A.GetFloat() - CpuOutputs.SurfacePixels[CellIndex].A.GetFloat()));
		AlphaMismatchCount += FMath::IsNearlyEqual(GpuOutputs.DensityPixels[CellIndex].A.GetFloat(), CpuOutputs.DensityPixels[CellIndex].A.GetFloat(), EncodingTolerance) ? 0 : 1;
	}

	int32 GpuRippleMismatches = 0;
	int32 GpuWetnessMismatches = 0;
	CountNeutralRippleAndWetness(GpuOutputs, GpuRippleMismatches, GpuWetnessMismatches);
	const double AlphaMismatchFraction = CpuOutputs.GetCellCount() > 0 ? static_cast<double>(AlphaMismatchCount) / CpuOutputs.GetCellCount() : 1.0;
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterPacking: %s max_depth_diff=%.5f max_terrain_diff=%.5f alpha_mismatch_fraction=%.5f"), *GpuLabel, MaximumDepthDifference, MaximumTerrainDifference, AlphaMismatchFraction));
	TestTrue(*(GpuLabel + TEXT(" depth channel differs from the CPU by at most 1e-3")), MaximumDepthDifference <= EncodingTolerance);
	TestTrue(*(GpuLabel + TEXT(" terrain channel differs from the CPU by at most 1e-3")), MaximumTerrainDifference <= EncodingTolerance);
	TestEqual(*(GpuLabel + TEXT(" water surface cells whose ripple channel is not 0.5")), GpuRippleMismatches, 0);
	TestEqual(*(GpuLabel + TEXT(" water surface cells whose wetness channel is not 0")), GpuWetnessMismatches, 0);
	TestTrue(*(GpuLabel + TEXT(" density alpha differs from the CPU in at most 1 percent of the cells")), AlphaMismatchFraction <= 0.01);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiProbeAcceptanceVerdictsMatchTest,
	"TDGame.FluidFXRhi.Water.Probe.AcceptanceVerdictsMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiProbeAcceptanceVerdictsMatchTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Probe.AcceptanceVerdictsMatch");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	const FString CpuLabel = CaseLabel + TEXT(" CPU 192");
	FTDFluidWaterProbeArm CpuArm;
	if (!RunProbeArm(*this, CpuLabel, ParityResolution, ETDFluidSolverBackend::Cpu, CpuArm))
	{
		return false;
	}
	ReportProbeMetrics(*this, CpuLabel, CpuArm.Metrics);
	ExpectProbeVerdictsPass(*this, CpuLabel, CpuArm.Metrics);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	const FString GpuLabel = CaseLabel + TEXT(" GPU 192");
	FTDFluidWaterProbeArm GpuArm;
	if (!RunProbeArm(*this, GpuLabel, ParityResolution, ETDFluidSolverBackend::Gpu, GpuArm))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, GpuLabel, GpuArm.Targets, GpuArm.FlowOutputs, ParityResolution) || !ExpectFlowActivity(*this, GpuLabel, GpuArm.FlowOutputs, GpuArm.Oracle))
	{
		return false;
	}

	ReportProbeMetrics(*this, GpuLabel, GpuArm.Metrics);
	const FTDFluidWaterProbeMetrics& CpuMetrics = CpuArm.Metrics;
	const FTDFluidWaterProbeMetrics& GpuMetrics = GpuArm.Metrics;
	TestTrue(*(GpuLabel + TEXT(" flow speed verdict matches the CPU")), GpuMetrics.PassesFlowSpeed() == CpuMetrics.PassesFlowSpeed());
	TestTrue(*(GpuLabel + TEXT(" riffle verdict matches the CPU")), GpuMetrics.PassesRiffle() == CpuMetrics.PassesRiffle());
	TestTrue(*(GpuLabel + TEXT(" rock wake verdict matches the CPU")), GpuMetrics.PassesRockWake() == CpuMetrics.PassesRockWake());
	TestTrue(*(GpuLabel + TEXT(" pawn ripple verdict matches the CPU")), GpuMetrics.PassesPawnRipple() == CpuMetrics.PassesPawnRipple());
	TestTrue(*(GpuLabel + TEXT(" foam drift verdict matches the CPU")), GpuMetrics.PassesFoamDrift() == CpuMetrics.PassesFoamDrift());
	TestTrue(*FString::Printf(TEXT("%s riffle ratio %.3f is within 10 percent of the CPU ratio %.3f"), *GpuLabel, GpuMetrics.SpeedRatio, CpuMetrics.SpeedRatio), FMath::Abs(GpuMetrics.SpeedRatio - CpuMetrics.SpeedRatio) <= 0.10 * CpuMetrics.SpeedRatio);
	TestTrue(*FString::Printf(TEXT("%s rock wake %.3f is within 25 percent of the CPU %.3f"), *GpuLabel, GpuMetrics.RockWakeWhitewater, CpuMetrics.RockWakeWhitewater), FMath::Abs(GpuMetrics.RockWakeWhitewater - CpuMetrics.RockWakeWhitewater) <= 0.25 * CpuMetrics.RockWakeWhitewater);
	TestTrue(*(GpuLabel + TEXT(" upstream shoulder lateral speeds keep the CPU signs and reach 3 cm/s")), GpuMetrics.UpperFlankLateral * CpuMetrics.UpperFlankLateral > 0.0 && GpuMetrics.LowerFlankLateral * CpuMetrics.LowerFlankLateral > 0.0 && FMath::Min(FMath::Abs(GpuMetrics.UpperFlankLateral), FMath::Abs(GpuMetrics.LowerFlankLateral)) >= 3.0 && FMath::Min(FMath::Abs(CpuMetrics.UpperFlankLateral), FMath::Abs(CpuMetrics.LowerFlankLateral)) >= 3.0);
	TestTrue(*FString::Printf(TEXT("%s foam shift %.1f cm is within 15 percent of the expected travel from the CPU shift %.1f cm"), *GpuLabel, GpuMetrics.FoamShiftCm, CpuMetrics.FoamShiftCm), FMath::Abs(GpuMetrics.FoamShiftCm - CpuMetrics.FoamShiftCm) <= 0.15 * ProbeFoamExpectedTravelCm);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiProbeAcceptanceVerdicts1024Test,
	"TDGame.FluidFXRhi.Water.Probe.AcceptanceVerdicts1024",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiProbeAcceptanceVerdicts1024Test::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Probe.AcceptanceVerdicts1024");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	const FString CpuLabel = CaseLabel + TEXT(" CPU 192");
	FTDFluidWaterProbeArm CpuArm;
	if (!RunProbeArm(*this, CpuLabel, ParityResolution, ETDFluidSolverBackend::Cpu, CpuArm))
	{
		return false;
	}
	ReportProbeMetrics(*this, CpuLabel, CpuArm.Metrics);
	ExpectProbeVerdictsPass(*this, CpuLabel, CpuArm.Metrics);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	const FString GpuLabel = CaseLabel + TEXT(" GPU 1024");
	FTDFluidWaterProbeArm GpuArm;
	if (!RunProbeArm(*this, GpuLabel, 1024, ETDFluidSolverBackend::Gpu, GpuArm))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, GpuLabel, GpuArm.Targets, GpuArm.FlowOutputs, 1024) || !ExpectFlowActivity(*this, GpuLabel, GpuArm.FlowOutputs, GpuArm.Oracle))
	{
		return false;
	}

	ReportProbeMetrics(*this, GpuLabel, GpuArm.Metrics);
	ExpectProbeVerdictsPass(*this, GpuLabel, GpuArm.Metrics);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiMemoryAllocationMatchesPlatformSizeTest,
	"TDGame.FluidFXRhi.Water.MemoryAllocationMatchesPlatformSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiMemoryAllocationMatchesPlatformSizeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.MemoryAllocationMatchesPlatformSize");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope VelocityFormatScope(WaterVelocityFormatVariableName, -1);
	FTDWaterGpuConsoleVariableScope RippleFormatScope(WaterRippleFormatVariableName, -1);
	FTDFluidGpuWaterRhiTestWorld MemoryWorld;
	const FTDFluidWaterSceneAssets Assets = LoadSceneAssets();
	if (!TestTrue(*(CaseLabel + TEXT(" loaded the engine cube mesh and shape material")), Assets.IsValid()))
	{
		return false;
	}
	SpawnColliderBlock(*MemoryWorld.World, *Assets.CubeMesh, MakeFloorBlock());

	const auto StepFrames = [&MemoryWorld](int32 FrameCount)
	{
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			MemoryWorld.StepWorld(StepSeconds60);
		}
	};

	StepFrames(2);
	const FTDFluidPoolUsage EmptyUsage = ReadPoolUsage();
	ATDFluidLiveActor* CalibrationActor = SpawnConfiguredLiveActor(*MemoryWorld.World, FVector(9000.0, 0.0, 0.0), MakeNonWaterConfig(512));
	if (!TestNotNull(*(CaseLabel + TEXT(" spawned the nonwater 512 calibration actor")), CalibrationActor))
	{
		return false;
	}
	StepFrames(3);
	const FTDFluidPoolUsage CalibrationUsage = ReadPoolUsage();
	const int64 CalibrationMeasuredBytes = (static_cast<int64>(CalibrationUsage.UsedKb) - static_cast<int64>(EmptyUsage.UsedKb)) * 1024;
	const int64 CalibrationExpectedBytes = SumPlatformBytes(MakeNonWaterStateDescs(512));
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterMemory: calibration measured=%lld expected=%lld whole_pool_kb=%u"), CalibrationMeasuredBytes, CalibrationExpectedBytes, CalibrationUsage.WholePoolKb));
	if (!CalibrationActor->LiveComponent->IsUsingGpuSolver() || FMath::Abs(CalibrationMeasuredBytes - CalibrationExpectedBytes) > PoolUsageToleranceBytes)
	{
		AddError(FString::Printf(TEXT("%s: pool usage calibration failed (harness defect, R16 step 1): measured %lld bytes, expected %lld bytes, GPU %d"), *CaseLabel, CalibrationMeasuredBytes, CalibrationExpectedBytes, CalibrationActor->LiveComponent->IsUsingGpuSolver() ? 1 : 0));
		return false;
	}

	MemoryWorld.World->DestroyActor(CalibrationActor);
	StepFrames(2);
	FTDFluidPoolUsage BaselineUsage = ReadPoolUsage();
	for (const int32 WaterResolution : { 192, 1024 })
	{
		const FString StageLabel = FString::Printf(TEXT("%s GPU %d"), *CaseLabel, WaterResolution);
		FTDFluidWaterSceneSpec MemoryScene;
		MemoryScene.Config = MakeWaterArmConfig(WaterResolution, ParityExtentCm);
		AActor* Anchor = SpawnSceneAnchor(*MemoryWorld.World, FVector::ZeroVector);
		ATDFluidWaterActor* WaterActor = Anchor ? SpawnSceneWaterActor(*MemoryWorld.World, *Anchor, MemoryScene, ETDFluidSolverBackend::Gpu) : nullptr;
		if (!TestNotNull(*(StageLabel + TEXT(" spawned the water actor")), WaterActor))
		{
			return false;
		}
		StepFrames(1);
		if (!WaterActor->LiveComponent->IsUsingGpuSolver())
		{
			AddError(MakeGpuNotSelectedMessage(StageLabel));
			return false;
		}
		StepFrames(2);

		const FTDFluidPoolUsage WaterUsage = ReadPoolUsage();
		const TArray<FRDGTextureDesc> StateDescs = MakeWaterStateDescs(WaterResolution);
		const int64 MeasuredBytes = (static_cast<int64>(WaterUsage.UsedKb) - static_cast<int64>(BaselineUsage.UsedKb)) * 1024;
		const int64 ExpectedPlatformBytes = SumPlatformBytes(StateDescs);
		const int64 LinearBytes = SumLinearBytes(StateDescs);
		ReportLine(*this, FString::Printf(
			TEXT("TDFluidGpuWaterMemory: %s state_measured=%lld state_platform=%lld state_linear=%lld output_platform=%lld transient_platform_partial=%lld whole_pool_kb_before=%u whole_pool_kb_after=%u"),
			*StageLabel,
			MeasuredBytes,
			ExpectedPlatformBytes,
			LinearBytes,
			SumPlatformBytes({ MakeOutputTargetDesc(WaterResolution), MakeOutputTargetDesc(WaterResolution), MakeOutputTargetDesc(WaterResolution) }),
			SumPlatformBytes(MakeWaterTransientDescs(WaterResolution)),
			BaselineUsage.WholePoolKb,
			WaterUsage.WholePoolKb));
		TestTrue(*FString::Printf(TEXT("%s pool usage difference %lld matches the platform size of the water state textures %lld within 64 KB"), *StageLabel, MeasuredBytes, ExpectedPlatformBytes), FMath::Abs(MeasuredBytes - ExpectedPlatformBytes) <= PoolUsageToleranceBytes);
		TestEqual(*(StageLabel + TEXT(" linear state bytes equal the VRAM table")), LinearBytes, GetTableLinearStateBytes(WaterResolution));

		MemoryWorld.World->DestroyActor(WaterActor);
		MemoryWorld.World->DestroyActor(Anchor);
		StepFrames(2);
		BaselineUsage = ReadPoolUsage();
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiRunToRunDeterministicTest,
	"TDGame.FluidFXRhi.Water.RunToRunDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiRunToRunDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.RunToRunDeterministic");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	static constexpr int32 DeterminismStepCount = 120;
	const auto HasCellVariance = [](const TArray<FFloat16Color>& Pixels)
	{
		for (const FFloat16Color& Pixel : Pixels)
		{
			if (FMemory::Memcmp(&Pixel, &Pixels[0], sizeof(FFloat16Color)) != 0)
			{
				return true;
			}
		}
		return false;
	};

	for (const int32 Resolution : { ParityResolution, 1024 })
	{
		const FTDFluidWaterSceneSpec DeterminismScene = MakeRockRiverScene(Resolution, FVector2D(-200.0, 0.0));
		FTDFluidWaterRiverArm Runs[2];
		for (int32 RunIndex = 0; RunIndex < 2; ++RunIndex)
		{
			const FString RunLabel = FString::Printf(TEXT("%s GPU %d run %d"), *CaseLabel, Resolution, RunIndex + 1);
			FTDFluidWaterRiverArm& Run = Runs[RunIndex];
			const bool bRan = RunWaterActorArm(*this, RunLabel, DeterminismScene, ETDFluidSolverBackend::Gpu, [&Run](FTDFluidWaterArmContext& ArmContext) -> bool
			{
				for (int32 StepIndex = 0; StepIndex < DeterminismStepCount; ++StepIndex)
				{
					for (const FTDFluidBrushStamp& Stamp : MakeOrbitWaterStamps(ArmContext.GetComponent().GetSimulationCenterWorld(), StepIndex))
					{
						ArmContext.QueueStamp(Stamp);
					}
					ArmContext.Step();
				}
				Run.Outputs = ArmContext.Capture();
				Run.Oracle = ArmContext.BuildOracle();
				Run.Targets = ArmContext.DescribeTargets();
				return true;
			});
			if (!bRan || !ExpectGpuArmOutputs(*this, RunLabel, Run.Targets, Run.Outputs, Resolution))
			{
				return false;
			}
		}

		const FString ResolutionLabel = FString::Printf(TEXT("%s GPU %d"), *CaseLabel, Resolution);
		if (!ExpectFlowActivity(*this, ResolutionLabel, Runs[0].Outputs, Runs[0].Oracle))
		{
			return false;
		}
		TestTrue(*(ResolutionLabel + TEXT(" density-pressure output varies across cells")), HasCellVariance(Runs[0].Outputs.DensityPixels));
		TestTrue(*(ResolutionLabel + TEXT(" velocity output varies across cells")), HasCellVariance(Runs[0].Outputs.VelocityPixels));
		TestTrue(*(ResolutionLabel + TEXT(" water surface output varies across cells")), HasCellVariance(Runs[0].Outputs.SurfacePixels));
		TestTrue(*(ResolutionLabel + TEXT(" density-pressure output is bit-identical across runs")), ArePixelsBitIdentical(Runs[0].Outputs.DensityPixels, Runs[1].Outputs.DensityPixels));
		TestTrue(*(ResolutionLabel + TEXT(" velocity output is bit-identical across runs")), ArePixelsBitIdentical(Runs[0].Outputs.VelocityPixels, Runs[1].Outputs.VelocityPixels));
		TestTrue(*(ResolutionLabel + TEXT(" water surface output is bit-identical across runs")), ArePixelsBitIdentical(Runs[0].Outputs.SurfacePixels, Runs[1].Outputs.SurfacePixels));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiBatchedWithSmokeKeepsWaterOutputsTest,
	"TDGame.FluidFXRhi.Water.BatchedWithSmokeKeepsWaterOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiBatchedWithSmokeKeepsWaterOutputsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.BatchedWithSmokeKeepsWaterOutputs");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	static constexpr int32 BatchStepCount = 120;
	const FTDFluidWaterSceneSpec BatchScene = MakeRockRiverScene(ParityResolution, FVector2D(-200.0, 0.0));
	FTDFluidWaterRiverArm SoloArm;
	FTDFluidWaterRiverArm BatchedArm;
	int32 SmokeGpuActorCount = 0;
	for (const bool bWithSmoke : { false, true })
	{
		const FString ArmLabel = FString::Printf(TEXT("%s GPU 192 %s"), *CaseLabel, bWithSmoke ? TEXT("batched with smoke") : TEXT("solo"));
		FTDFluidWaterRiverArm& Arm = bWithSmoke ? BatchedArm : SoloArm;
		const bool bRan = RunWaterActorArm(*this, ArmLabel, BatchScene, ETDFluidSolverBackend::Gpu, [&Arm, &SmokeGpuActorCount, bWithSmoke](FTDFluidWaterArmContext& ArmContext) -> bool
		{
			TArray<ATDFluidLiveActor*> SmokeActors;
			const int32 SmokeResolutions[] = { 128, 192, 256 };
			for (int32 SmokeIndex = 0; SmokeIndex < (bWithSmoke ? 3 : 0); ++SmokeIndex)
			{
				SmokeActors.Add(SpawnConfiguredLiveActor(*ArmContext.ArmWorld->World, FVector(20000.0 + 8000.0 * SmokeIndex, 0.0, 0.0), MakeNonWaterConfig(SmokeResolutions[SmokeIndex])));
			}
			for (int32 StepIndex = 0; StepIndex < BatchStepCount; ++StepIndex)
			{
				for (const FTDFluidBrushStamp& Stamp : MakeOrbitWaterStamps(ArmContext.GetComponent().GetSimulationCenterWorld(), StepIndex))
				{
					ArmContext.QueueStamp(Stamp);
				}
				for (ATDFluidLiveActor* SmokeActor : SmokeActors)
				{
					if (SmokeActor)
					{
						SmokeActor->LiveComponent->QueueBrushStamp(MakeOrbitSmokeStamp(SmokeActor->GetActorLocation(), StepIndex));
					}
				}
				ArmContext.Step();
			}
			for (const ATDFluidLiveActor* SmokeActor : SmokeActors)
			{
				SmokeGpuActorCount += SmokeActor && SmokeActor->LiveComponent->IsUsingGpuSolver() ? 1 : 0;
			}
			Arm.Outputs = ArmContext.Capture();
			Arm.Oracle = ArmContext.BuildOracle();
			Arm.Targets = ArmContext.DescribeTargets();
			return true;
		});
		if (!bRan || !ExpectGpuArmOutputs(*this, ArmLabel, Arm.Targets, Arm.Outputs, ParityResolution))
		{
			return false;
		}
	}

	TestEqual(*(CaseLabel + TEXT(" smoke actors running on the GPU beside the water actor")), SmokeGpuActorCount, 3);
	if (!ExpectFlowActivity(*this, CaseLabel + TEXT(" GPU 192 batched with smoke"), BatchedArm.Outputs, BatchedArm.Oracle))
	{
		return false;
	}
	TestTrue(*(CaseLabel + TEXT(" density-pressure output is bit-identical to the solo run")), ArePixelsBitIdentical(SoloArm.Outputs.DensityPixels, BatchedArm.Outputs.DensityPixels));
	TestTrue(*(CaseLabel + TEXT(" velocity output is bit-identical to the solo run")), ArePixelsBitIdentical(SoloArm.Outputs.VelocityPixels, BatchedArm.Outputs.VelocityPixels));
	TestTrue(*(CaseLabel + TEXT(" water surface output is bit-identical to the solo run")), ArePixelsBitIdentical(SoloArm.Outputs.SurfacePixels, BatchedArm.Outputs.SurfacePixels));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiSlidingWindowParityTest,
	"TDGame.FluidFXRhi.Water.SlidingWindowParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiSlidingWindowParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.SlidingWindowParity");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	static constexpr int32 SlideStepCount = 600;
	static constexpr double SlideDistanceCm = 600.0;
	const FTDFluidWaterSceneSpec SlideScene = MakeRiverScene(ParityResolution, ParityExtentCm, StepSeconds60, MakeStraightRiver(-3000.0, 4200.0, 400.0f, 200.0f));

	struct FTDFluidWaterStripSample
	{
		FVector2D MeanVelocity = FVector2D::ZeroVector;
		FVector2D MeanTargetFlow = FVector2D::ZeroVector;
		int32 CellCount = 0;
	};

	struct FTDFluidWaterSlideArm
	{
		TMap<int32, FTDFluidWaterStripSample> StripSamples;
		FTDFluidWaterArmOutputs Outputs;
		FTDFluidWaterGuideOracle Oracle;
		FTDFluidWaterTargetInfo Targets;
	};

	const auto RunSlideArm = [this, &SlideScene](const FString& ArmLabel, ETDFluidSolverBackend Backend, FTDFluidWaterSlideArm& OutArm)
	{
		return RunWaterActorArm(*this, ArmLabel, SlideScene, Backend, [&OutArm](FTDFluidWaterArmContext& ArmContext) -> bool
		{
			FVector2D PreviousCenter = ArmContext.GetComponent().GetSimulationCenterWorld();
			for (int32 StepIndex = 0; StepIndex < SlideStepCount; ++StepIndex)
			{
				ArmContext.Anchor->SetActorLocation(FVector(SlideDistanceCm * (StepIndex + 1) / SlideStepCount, 0.0, 0.0));
				ArmContext.Step();
				const FVector2D CurrentCenter = ArmContext.GetComponent().GetSimulationCenterWorld();
				const FIntPoint ShiftCells(FMath::RoundToInt32((CurrentCenter.X - PreviousCenter.X) / ParityCellSizeCm), FMath::RoundToInt32((CurrentCenter.Y - PreviousCenter.Y) / ParityCellSizeCm));
				PreviousCenter = CurrentCenter;
				if (ShiftCells == FIntPoint::ZeroValue)
				{
					continue;
				}

				const FTDFluidWaterArmOutputs Outputs = ArmContext.Capture();
				const FTDFluidWaterGuideOracle Oracle = ArmContext.BuildOracle();
				FTDFluidWaterStripSample& Sample = OutArm.StripSamples.Add(StepIndex);
				for (int32 CellY = 0; CellY < Outputs.Resolution && Outputs.HasAllPixels(); ++CellY)
				{
					for (int32 CellX = 0; CellX < Outputs.Resolution; ++CellX)
					{
						const bool bIsNewColumn = (ShiftCells.X > 0 && CellX >= Outputs.Resolution - ShiftCells.X) || (ShiftCells.X < 0 && CellX < -ShiftCells.X);
						const bool bIsNewRow = (ShiftCells.Y > 0 && CellY >= Outputs.Resolution - ShiftCells.Y) || (ShiftCells.Y < 0 && CellY < -ShiftCells.Y);
						if (!bIsNewColumn && !bIsNewRow)
						{
							continue;
						}
						const int32 OracleIndex = Oracle.FindCellIndex(Outputs.GetCellCenterWorld(CellX, CellY));
						Sample.MeanVelocity += Outputs.DecodeVelocity(CellY * Outputs.Resolution + CellX);
						Sample.MeanTargetFlow += OracleIndex != INDEX_NONE ? Oracle.Targets[OracleIndex] * Oracle.Weights[OracleIndex] : FVector2D::ZeroVector;
						++Sample.CellCount;
					}
				}
				if (Sample.CellCount > 0)
				{
					Sample.MeanVelocity /= Sample.CellCount;
					Sample.MeanTargetFlow /= Sample.CellCount;
				}
			}
			OutArm.Outputs = ArmContext.Capture();
			OutArm.Oracle = ArmContext.BuildOracle();
			OutArm.Targets = ArmContext.DescribeTargets();
			return true;
		});
	};

	const FString CpuLabel = CaseLabel + TEXT(" CPU 192");
	FTDFluidWaterSlideArm CpuArm;
	if (!RunSlideArm(CpuLabel, ETDFluidSolverBackend::Cpu, CpuArm))
	{
		return false;
	}

	const FTDFluidWaterOutputHealth CpuHealth = MeasureOutputHealth(CpuArm.Outputs);
	const TArray<FTDFluidWaterSectionFlux> CpuSections = MeasureDischargeSections(CpuArm.Outputs, CpuArm.Oracle);
	ReportSections(*this, CpuLabel, CpuSections);
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterSlide: %s shift_frames=%d non_finite=%d"), *CpuLabel, CpuArm.StripSamples.Num(), CpuHealth.NonFiniteChannelCount));
	TestTrue(*(CpuLabel + TEXT(" captured every packed output pixel")), CpuArm.Outputs.HasAllPixels());
	TestTrue(*(CpuLabel + TEXT(" window shifted while the anchor moved 6 m")), CpuArm.StripSamples.Num() > 0);
	TestEqual(*(CpuLabel + TEXT(" output channels with non-finite values after sliding")), CpuHealth.NonFiniteChannelCount, 0);
	for (int32 SectionIndex = 0; SectionIndex < CpuSections.Num(); ++SectionIndex)
	{
		TestTrue(*FString::Printf(TEXT("%s section %d target flux is positive after sliding"), *CpuLabel, DischargeSectionColumns[SectionIndex]), CpuSections[SectionIndex].TargetFlux > 1000.0);
	}
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	const FString GpuLabel = CaseLabel + TEXT(" GPU 192");
	FTDFluidWaterSlideArm GpuArm;
	if (!RunSlideArm(GpuLabel, ETDFluidSolverBackend::Gpu, GpuArm))
	{
		return false;
	}
	if (!ExpectGpuArmOutputs(*this, GpuLabel, GpuArm.Targets, GpuArm.Outputs, ParityResolution) || !ExpectFlowActivity(*this, GpuLabel, GpuArm.Outputs, GpuArm.Oracle))
	{
		return false;
	}

	const FTDFluidWaterOutputHealth GpuHealth = MeasureOutputHealth(GpuArm.Outputs);
	TestEqual(*(GpuLabel + TEXT(" output channels with non-finite values after sliding")), GpuHealth.NonFiniteChannelCount, 0);
	TestEqual(*(GpuLabel + TEXT(" shift frame count equals the CPU")), GpuArm.StripSamples.Num(), CpuArm.StripSamples.Num());
	double LargestStripError = 0.0;
	for (const TPair<int32, FTDFluidWaterStripSample>& CpuEntry : CpuArm.StripSamples)
	{
		const FTDFluidWaterStripSample* GpuSample = GpuArm.StripSamples.Find(CpuEntry.Key);
		if (!TestNotNull(*FString::Printf(TEXT("%s shifted at the CPU shift step %d"), *GpuLabel, CpuEntry.Key), GpuSample))
		{
			continue;
		}
		const double StripError = (GpuSample->MeanVelocity - CpuEntry.Value.MeanVelocity).Size();
		const double StripLimit = 0.05 * CpuEntry.Value.MeanTargetFlow.Size() + 1.0;
		LargestStripError = FMath::Max(LargestStripError, StripError);
		TestTrue(*FString::Printf(TEXT("%s step %d new strip mean velocity differs from the CPU by %.3f cm/s (limit %.3f)"), *GpuLabel, CpuEntry.Key, StripError, StripLimit), StripError <= StripLimit);
	}
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterSlide: %s shift_frames=%d largest_strip_error=%.3f"), *GpuLabel, GpuArm.StripSamples.Num(), LargestStripError));

	const TArray<FTDFluidWaterSectionFlux> GpuSections = MeasureDischargeSections(GpuArm.Outputs, GpuArm.Oracle);
	ReportSections(*this, GpuLabel, GpuSections);
	ExpectDischargeParity(*this, GpuLabel, CpuSections, GpuSections);
	return !HasAnyErrors();
}

namespace TDFluidGpuWaterRhiTestPrivate
{
	void ReportCpuBankBandFloor(FAutomationTestBase& Test, const FString& CaseLabel, double BorderBandCm, double Cpu192BandL2, int32 StepCount)
	{
		constexpr int32 FloorResolution = 512;
		const FString FloorLabel = FString::Printf(TEXT("%s CPU %d"), *CaseLabel, FloorResolution);
		FTDFluidWaterRiverArm FloorArm;
		if (!RunRiverArm(Test, FloorLabel, MakeRockRiverScene(FloorResolution, FVector2D::ZeroVector), ETDFluidSolverBackend::Cpu, StepCount, FloorArm))
		{
			ReportLine(Test, FString::Printf(TEXT("TDFluidGpuWaterBankBandFloor: %s cpu512 floor unavailable"), *CaseLabel));
			return;
		}

		const FTDFluidWaterDivergence FloorDivergence = MeasureOutputDivergence(FloorArm.Outputs, BorderBandCm);
		ReportLine(Test, FString::Printf(TEXT("TDFluidGpuWaterBankBandFloor: cpu512_band_l2=%.5f ratio_to_cpu192=%.4f limit_ratio=1.5 band_cells=%d"), FloorDivergence.BandL2, FloorDivergence.BandL2 / FMath::Max(Cpu192BandL2, UE_DOUBLE_SMALL_NUMBER), FloorDivergence.BandCellCount));
	}

	bool RunDivergenceBound(FAutomationTestBase& Test, const FString& CaseLabel, bool bChecksBankBand)
	{
		if (!CanRunWaterRhiTest(Test, CaseLabel))
		{
			return false;
		}

		FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
		FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
		constexpr int32 DivergenceStepCount = 600;
		const double BorderBandCm = 0.1 * ParityExtentCm;
		const FString CpuLabel = CaseLabel + TEXT(" CPU 192");
		FTDFluidWaterRiverArm CpuArm;
		if (!RunRiverArm(Test, CpuLabel, MakeRockRiverScene(ParityResolution, FVector2D::ZeroVector), ETDFluidSolverBackend::Cpu, DivergenceStepCount, CpuArm))
		{
			return false;
		}

		const FTDFluidWaterDivergence CpuDivergence = MeasureOutputDivergence(CpuArm.Outputs, BorderBandCm);
		ReportLine(Test, FString::Printf(TEXT("TDFluidGpuWaterDivergence: %s interior_l2=%.5f interior_cells=%d band_l2=%.5f band_cells=%d"), *CpuLabel, CpuDivergence.InteriorL2, CpuDivergence.InteriorCellCount, CpuDivergence.BandL2, CpuDivergence.BandCellCount));
		Test.TestTrue(*(CpuLabel + TEXT(" measured interior fluid cells")), CpuDivergence.InteriorCellCount > 0);
		Test.TestTrue(*(CpuLabel + TEXT(" measured bank band fluid cells")), CpuDivergence.BandCellCount > 0);
		Test.TestTrue(*(CpuLabel + TEXT(" divergence is finite")), FMath::IsFinite(CpuDivergence.InteriorL2) && FMath::IsFinite(CpuDivergence.BandL2));
		if (bChecksBankBand && FPlatformMisc::GetEnvironmentVariable(DiagnosticFloorsEnvironmentName) == TEXT("1"))
		{
			ReportCpuBankBandFloor(Test, CaseLabel, BorderBandCm, CpuDivergence.BandL2, DivergenceStepCount);
		}
		if (!ExpectWaterGpuRegistered(Test, CaseLabel, WaterGpuScope))
		{
			return false;
		}

		double HalfResolutionFieldBandL2 = 0.0;
		for (const int32 GpuResolution : { 512, 1024 })
		{
			const FString GpuLabel = FString::Printf(TEXT("%s GPU %d"), *CaseLabel, GpuResolution);
			FTDFluidWaterRiverArm GpuArm;
			if (!RunRiverArm(Test, GpuLabel, MakeRockRiverScene(GpuResolution, FVector2D::ZeroVector), ETDFluidSolverBackend::Gpu, DivergenceStepCount, GpuArm))
			{
				return false;
			}
			if (!ExpectGpuArmOutputs(Test, GpuLabel, GpuArm.Targets, GpuArm.Outputs, GpuResolution) || !ExpectFlowActivity(Test, GpuLabel, GpuArm.Outputs, GpuArm.Oracle))
			{
				return false;
			}

			const FTDFluidWaterDivergence GpuDivergence = MeasureOutputDivergence(GpuArm.Outputs, BorderBandCm);
			ReportLine(Test, FString::Printf(TEXT("TDFluidGpuWaterDivergence: %s interior_l2=%.5f interior_cells=%d band_l2=%.5f band_cells=%d"), *GpuLabel, GpuDivergence.InteriorL2, GpuDivergence.InteriorCellCount, GpuDivergence.BandL2, GpuDivergence.BandCellCount));
			ReportPressureVariables(Test, GpuLabel);
			if (bChecksBankBand)
			{
				constexpr double BankBandRatioLimit = 1.5;
				constexpr double QuarterFieldBankBandRatioLimit = 1.75;
				constexpr double QuarterToHalfFieldBankBandRatioLimit = 1.3;
				const bool bUsesQuarterResolutionField = ComputeWaterFieldResolution(GpuResolution) * 4 == GpuResolution;
				const double RatioLimit = bUsesQuarterResolutionField ? QuarterFieldBankBandRatioLimit : BankBandRatioLimit;
				Test.TestTrue(*FString::Printf(TEXT("%s bank band divergence %.5f is at most %.2f times the CPU 192 %.5f"), *GpuLabel, GpuDivergence.BandL2, RatioLimit, CpuDivergence.BandL2), GpuDivergence.BandCellCount > 0 && GpuDivergence.BandL2 <= RatioLimit * CpuDivergence.BandL2);
				if (ComputeWaterFieldResolution(GpuResolution) * 2 == GpuResolution)
				{
					HalfResolutionFieldBandL2 = GpuDivergence.BandL2;
				}
				if (bUsesQuarterResolutionField)
				{
					Test.TestTrue(*FString::Printf(TEXT("%s bank band divergence %.5f is at most %.2f times the half resolution field GPU %.5f"), *GpuLabel, GpuDivergence.BandL2, QuarterToHalfFieldBankBandRatioLimit, HalfResolutionFieldBandL2), HalfResolutionFieldBandL2 > 0.0 && GpuDivergence.BandL2 <= QuarterToHalfFieldBankBandRatioLimit * HalfResolutionFieldBandL2);
				}
				continue;
			}
			Test.TestTrue(*FString::Printf(TEXT("%s interior divergence %.5f is at most 1.2 times the CPU 192 %.5f"), *GpuLabel, GpuDivergence.InteriorL2, CpuDivergence.InteriorL2), GpuDivergence.InteriorCellCount > 0 && GpuDivergence.InteriorL2 <= 1.2 * CpuDivergence.InteriorL2);
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiDivergenceWithinCpuBoundTest,
	"TDGame.FluidFXRhi.Water.Pressure.DivergenceWithinCpuBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiDivergenceWithinCpuBoundTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;
	return RunDivergenceBound(*this, TEXT("TDGame.FluidFXRhi.Water.Pressure.DivergenceWithinCpuBound"), false) && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiDivergenceBankBandWithinCpuBoundTest,
	"TDGame.FluidFXRhi.Water.Pressure.DivergenceBankBandWithinCpuBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiDivergenceBankBandWithinCpuBoundTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;
	return RunDivergenceBound(*this, TEXT("TDGame.FluidFXRhi.Water.Pressure.DivergenceBankBandWithinCpuBound"), true) && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiNoSurfaceStripStaysSolidTest,
	"TDGame.FluidFXRhi.Water.Pressure.NoSurfaceStripStaysSolid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiNoSurfaceStripStaysSolidTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Pressure.NoSurfaceStripStaysSolid");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	constexpr int32 StripStepCount = 30;
	for (const int32 GpuResolution : { 512, 1024 })
	{
		const FString GpuLabel = FString::Printf(TEXT("%s GPU %d"), *CaseLabel, GpuResolution);
		const double FieldCellSizeCm = ParityExtentCm / ComputeWaterFieldResolution(GpuResolution);
		FTDFluidWaterRiverArm StripArm;
		if (!RunRiverArm(*this, GpuLabel, MakeNoSurfaceStripScene(GpuResolution), ETDFluidSolverBackend::Gpu, StripStepCount, StripArm))
		{
			return false;
		}
		if (!ExpectGpuArmOutputs(*this, GpuLabel, StripArm.Targets, StripArm.Outputs, GpuResolution) || !ExpectStillActivity(*this, GpuLabel, StripArm.Outputs, MakeStripRegionMask(StripArm.Outputs, FieldCellSizeCm)))
		{
			return false;
		}

		const FTDFluidWaterArmOutputs& Outputs = StripArm.Outputs;
		const double FineCellSizeCm = Outputs.GetCellSizeCm().X;
		int32 StripCellCount = 0;
		int32 StripCellsWithAlpha = 0;
		int32 SideWetCellCount = 0;
		int32 SideWetCellsWithAlpha = 0;
		for (int32 CellY = 0; CellY < Outputs.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Outputs.Resolution; ++CellX)
			{
				const int32 CellIndex = CellY * Outputs.Resolution + CellX;
				const double CellWorldX = Outputs.GetCellCenterWorld(CellX, CellY).X;
				if (CellWorldX > 0.0 && CellWorldX < FieldCellSizeCm)
				{
					++StripCellCount;
					StripCellsWithAlpha += Outputs.IsSolid(CellIndex) ? 0 : 1;
					continue;
				}

				const bool bIsBesideStrip = (CellWorldX < 0.0 && CellWorldX > -FineCellSizeCm) || (CellWorldX > FieldCellSizeCm && CellWorldX < FieldCellSizeCm + FineCellSizeCm);
				if (!bIsBesideStrip || Outputs.GetDepthEncoding(CellIndex) <= 0.0f)
				{
					continue;
				}
				++SideWetCellCount;
				SideWetCellsWithAlpha += Outputs.IsSolid(CellIndex) ? 0 : 1;
			}
		}

		const double SideAliveFraction = SideWetCellCount > 0 ? static_cast<double>(SideWetCellsWithAlpha) / SideWetCellCount : 0.0;
		ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterStrip: %s strip_cells=%d strip_with_alpha=%d side_wet=%d side_alive_fraction=%.4f"), *GpuLabel, StripCellCount, StripCellsWithAlpha, SideWetCellCount, SideAliveFraction));
		TestTrue(*(GpuLabel + TEXT(" has fine cells inside the one field cell strip")), StripCellCount > 0);
		TestEqual(*(GpuLabel + TEXT(" fine cells inside the no surface strip with a nonzero density alpha")), StripCellsWithAlpha, 0);
		TestTrue(*(GpuLabel + TEXT(" has wet fine cells beside the strip")), SideWetCellCount > 0);
		TestTrue(*(GpuLabel + TEXT(" at least 90 percent of the wet fine cells beside the strip keep a density alpha")), SideAliveFraction >= 0.9);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterRhiOptimizationsKeepOutputsTest,
	"TDGame.FluidFXRhi.Water.OptimizationsKeepOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterRhiOptimizationsKeepOutputsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.OptimizationsKeepOutputs");
	if (!CanRunWaterRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	if (!ExpectWaterGpuRegistered(*this, CaseLabel, WaterGpuScope))
	{
		return false;
	}

	static constexpr int32 OptimizationStepCount = 120;
	const TCHAR* const OptimizationVariableNames[] = {
		TEXT("TD.FluidFX.WaterGpuWideOpenBorderScan"),
		TEXT("TD.FluidFX.WaterGpuSolidFirstReads"),
		TEXT("TD.FluidFX.WaterGpuSkipOpenPressureMean"),
		TEXT("TD.FluidFX.WaterGpuSkipRippleParityCopy"),
		TEXT("TD.FluidFX.WaterGpuSkipDryPressureTiles"),
		TEXT("TD.FluidFX.WaterGpuFusedPressureTail"),
		TEXT("TD.FluidFX.WaterGpuFusedPrestep"),
		TEXT("TD.FluidFX.WaterGpuSharedHalfApertures"),
		TEXT("TD.FluidFX.WaterGpuSkipInactiveMultigridTiles"),
	};
	const int32 VariableCount = static_cast<int32>(UE_ARRAY_COUNT(OptimizationVariableNames));
	const float StepCycle[] = { StepSeconds60, StepSeconds144, LargeStepSeconds, StepSeconds60, StepSeconds144 };

	struct FTDWaterOptimizationMode
	{
		FString Label;
		TArray<int32> Values;
	};
	const auto MakeUniformMode = [VariableCount](const TCHAR* Label, int32 Value)
	{
		FTDWaterOptimizationMode Mode;
		Mode.Label = Label;
		Mode.Values.Init(Value, VariableCount);
		return Mode;
	};

	for (const int32 Resolution : { ParityResolution, 1024 })
	{
		FTDFluidWaterSceneSpec OptimizationScene = MakeRockRiverScene(Resolution, FVector2D(-200.0, 0.0));
		OptimizationScene.Config.RippleWaveSpeedCmPerSec = 120.0f;
		OptimizationScene.Config.RippleDampingPerSec = 1.2f;
		OptimizationScene.Config.RippleFlowAdvection = 1.0f;

		TArray<FTDWaterOptimizationMode> Modes;
		Modes.Add(MakeUniformMode(TEXT("all off"), 0));
		Modes.Add(MakeUniformMode(TEXT("all on"), 1));
		if (Resolution == ParityResolution)
		{
			for (int32 VariableIndex = 0; VariableIndex < VariableCount; ++VariableIndex)
			{
				FTDWaterOptimizationMode& SingleMode = Modes.AddDefaulted_GetRef();
				SingleMode = MakeUniformMode(TEXT(""), 0);
				SingleMode.Label = FString::Printf(TEXT("only %s"), OptimizationVariableNames[VariableIndex]);
				SingleMode.Values[VariableIndex] = 1;
			}
		}

		TArray<FTDFluidWaterRiverArm> Arms;
		Arms.SetNum(Modes.Num());
		for (int32 ModeIndex = 0; ModeIndex < Modes.Num(); ++ModeIndex)
		{
			TArray<TUniquePtr<FTDWaterGpuConsoleVariableScope>> Scopes;
			for (int32 VariableIndex = 0; VariableIndex < VariableCount; ++VariableIndex)
			{
				Scopes.Add(MakeUnique<FTDWaterGpuConsoleVariableScope>(OptimizationVariableNames[VariableIndex], Modes[ModeIndex].Values[VariableIndex]));
				if (!TestTrue(*FString::Printf(TEXT("%s GPU %d registered %s"), *CaseLabel, Resolution, OptimizationVariableNames[VariableIndex]), Scopes.Last()->IsValid()))
				{
					return false;
				}
			}

			const FString RunLabel = FString::Printf(TEXT("%s GPU %d %s"), *CaseLabel, Resolution, *Modes[ModeIndex].Label);
			FTDFluidWaterRiverArm& Arm = Arms[ModeIndex];
			const bool bRan = RunWaterActorArm(*this, RunLabel, OptimizationScene, ETDFluidSolverBackend::Gpu, [&Arm, &StepCycle](FTDFluidWaterArmContext& ArmContext) -> bool
			{
				for (int32 StepIndex = 0; StepIndex < OptimizationStepCount; ++StepIndex)
				{
					for (const FTDFluidBrushStamp& Stamp : MakeOrbitWaterStamps(ArmContext.GetComponent().GetSimulationCenterWorld(), StepIndex))
					{
						ArmContext.QueueStamp(Stamp);
					}
					ArmContext.StepFor(StepCycle[StepIndex % UE_ARRAY_COUNT(StepCycle)]);
				}
				Arm.Outputs = ArmContext.Capture();
				Arm.Oracle = ArmContext.BuildOracle();
				Arm.Targets = ArmContext.DescribeTargets();
				return true;
			});
			if (!bRan || !ExpectGpuArmOutputs(*this, RunLabel, Arm.Targets, Arm.Outputs, Resolution))
			{
				return false;
			}
		}

		const FString ResolutionLabel = FString::Printf(TEXT("%s GPU %d"), *CaseLabel, Resolution);
		if (!ExpectFlowActivity(*this, ResolutionLabel, Arms[0].Outputs, Arms[0].Oracle))
		{
			return false;
		}

		for (int32 ModeIndex = 1; ModeIndex < Modes.Num(); ++ModeIndex)
		{
			const FString ModeLabel = ResolutionLabel + TEXT(" ") + Modes[ModeIndex].Label;
			const bool bDensityMatches = ArePixelsBitIdentical(Arms[0].Outputs.DensityPixels, Arms[ModeIndex].Outputs.DensityPixels);
			const bool bVelocityMatches = ArePixelsBitIdentical(Arms[0].Outputs.VelocityPixels, Arms[ModeIndex].Outputs.VelocityPixels);
			const bool bSurfaceMatches = ArePixelsBitIdentical(Arms[0].Outputs.SurfacePixels, Arms[ModeIndex].Outputs.SurfacePixels);
			ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterOptimizations: %s density_identical=%d velocity_identical=%d surface_identical=%d"), *ModeLabel, bDensityMatches ? 1 : 0, bVelocityMatches ? 1 : 0, bSurfaceMatches ? 1 : 0));
			TestTrue(*(ModeLabel + TEXT(" density-pressure output is bit-identical to all off")), bDensityMatches);
			TestTrue(*(ModeLabel + TEXT(" velocity output is bit-identical to all off")), bVelocityMatches);
			TestTrue(*(ModeLabel + TEXT(" water surface output is bit-identical to all off")), bSurfaceMatches);
		}
	}
	return !HasAnyErrors();
}

#endif
