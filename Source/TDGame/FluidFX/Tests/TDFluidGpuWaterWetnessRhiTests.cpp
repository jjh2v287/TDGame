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
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterActor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidGpuWaterWetnessRhiTests, Log, All);

namespace TDFluidGpuWaterWetnessRhiTestPrivate
{
	constexpr float StepSeconds60 = 1.0f / 60.0f;
	constexpr float StepSeconds240 = 1.0f / 240.0f;
	constexpr int32 CoastResolution = 192;
	constexpr int32 LargeCoastResolution = 1024;
	constexpr int32 LargeFieldDivisor = 4;
	constexpr double CoastExtentCm = 3600.0;
	constexpr double CoastSlopeDegrees = 5.0;
	constexpr float FlatFloorTopCm = -100.0f;
	constexpr float HighSeaLevelCm = 0.0f;
	constexpr float LoweredSeaLevelCm = -50.0f;
	constexpr float ClassificationMarginCm = 2.0f;
	constexpr float DryingSeconds = 8.0f;
	constexpr float SlowDryingSeconds = 30.0f;
	constexpr float SwashHeightCm = 25.0f;
	constexpr float BankHeightCm = 10.0f;
	constexpr float CoastPeriodSeconds = 6.0f;
	constexpr float SwashPeakPhase = 0.25f;
	constexpr float ParityTolerance = 2.0e-3f;
	constexpr float ShapeTolerance = 3.0e-3f;
	constexpr float ProbeTolerance = 1.0e-3f;
	constexpr float RelativeDryingTolerance = 0.02f;
	constexpr int32 TerrainWaitTickLimit = 900;
	constexpr int32 AlignedTickCount = 300;
	constexpr int32 SettleStepCount = 3;
	constexpr int32 TimeConstantSteps60 = 480;
	constexpr int32 ThreeTimeConstantSteps60 = 1440;
	constexpr int32 SwashParityStepCount = 400;
	constexpr int32 HighFrameRateStepCount = 2400;
	constexpr int32 SlideCellCount = 12;
	constexpr int32 SlideCount = 3;
	constexpr int32 SlideSettleStepCount = 6;
	constexpr int32 SmokeActorCount = 3;
	constexpr int32 BatchStepCount = 240;
	constexpr int32 SplashMeasureStepCount = 3;
	constexpr int32 WhitewaterStepCount = 300;
	constexpr float SplashRadiusCm = 60.0f;
	constexpr float WhitewaterRetentionTolerance = 0.01f;
	constexpr double SelectiveRetention = 0.17475;
	constexpr double LaceRetentionSeaDissipation = 0.12;
	const TCHAR* const WaterGpuVariableName = TEXT("TD.FluidFX.WaterGpu");
	const TCHAR* const SolverBackendVariableName = TEXT("TD.FluidFX.SolverBackend");
	const TCHAR* const WetnessConsoleVariableName = TEXT("TD.FluidFX.WaterWetness");
	const TCHAR* const EnableWetnessFieldName = TEXT("bEnableWaterWetness");
	const TCHAR* const WhitewaterOverrideFieldName = TEXT("bUseWhitewaterDissipation");
	const TCHAR* const WhitewaterRateFieldName = TEXT("WhitewaterDissipation");
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

	FString MakeMissingConfigFieldMessage(const FString& Label, const TCHAR* FieldName)
	{
		return FString::Printf(TEXT("%s: FTDFluidSimulationConfig::%s is missing (W-2s-a not implemented)"), *Label, FieldName);
	}

	FString MakeMissingConsoleVariableMessage(const FString& Label)
	{
		return FString::Printf(TEXT("%s: console variable TD.FluidFX.WaterWetness is not registered (W-2s-a not implemented)"), *Label);
	}

	FString MakeMissingWaterGpuMessage(const FString& Label)
	{
		return FString::Printf(TEXT("%s: TD.FluidFX.WaterGpu is not registered, so the water layer still forces the CPU solver"), *Label);
	}

	FString MakeGpuNotSelectedMessage(const FString& Label)
	{
		return FString::Printf(TEXT("%s: the GPU water actor did not select the GPU solver (harness defect)"), *Label);
	}

	FString MakeTerrainTimeoutMessage(const FString& Label)
	{
		return FString::Printf(TEXT("%s: terrain sampling did not complete within %d ticks (harness defect)"), *Label, TerrainWaitTickLimit);
	}

	bool TrySetConfigBool(FTDFluidSimulationConfig& Config, const TCHAR* FieldName, bool bValue)
	{
		const FBoolProperty* Property = FindFProperty<FBoolProperty>(FTDFluidSimulationConfig::StaticStruct(), FieldName);
		if (!Property)
		{
			return false;
		}

		Property->SetPropertyValue_InContainer(&Config, bValue);
		return true;
	}

	bool TrySetConfigFloat(FTDFluidSimulationConfig& Config, const TCHAR* FieldName, float Value)
	{
		const FFloatProperty* Property = FindFProperty<FFloatProperty>(FTDFluidSimulationConfig::StaticStruct(), FieldName);
		if (!Property)
		{
			return false;
		}

		Property->SetPropertyValue_InContainer(&Config, Value);
		return true;
	}

	bool RequireConfigBool(FAutomationTestBase& Test, const FString& Label, FTDFluidSimulationConfig& Config, const TCHAR* FieldName, bool bValue)
	{
		if (TrySetConfigBool(Config, FieldName, bValue))
		{
			return true;
		}

		Test.AddError(MakeMissingConfigFieldMessage(Label, FieldName));
		return false;
	}

	bool RequireConfigFloat(FAutomationTestBase& Test, const FString& Label, FTDFluidSimulationConfig& Config, const TCHAR* FieldName, float Value)
	{
		if (TrySetConfigFloat(Config, FieldName, Value))
		{
			return true;
		}

		Test.AddError(MakeMissingConfigFieldMessage(Label, FieldName));
		return false;
	}

	struct FTDWetnessConsoleVariableScope
	{
		FTDWetnessConsoleVariableScope(const TCHAR* Name, int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (!Variable)
			{
				return;
			}

			PreviousValue = Variable->GetString();
			Variable->Set(Value, ECVF_SetByCode);
		}

		~FTDWetnessConsoleVariableScope()
		{
			if (Variable)
			{
				Variable->Set(*PreviousValue, ECVF_SetByCode);
			}
		}

		FTDWetnessConsoleVariableScope(const FTDWetnessConsoleVariableScope&) = delete;
		FTDWetnessConsoleVariableScope& operator=(const FTDWetnessConsoleVariableScope&) = delete;

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

	struct FTDWetnessCaseScopes
	{
		FTDWetnessConsoleVariableScope SolverBackendScope{ SolverBackendVariableName, 0 };
		FTDWetnessConsoleVariableScope WaterGpuScope{ WaterGpuVariableName, 1 };
	};

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	bool CanRunWetnessRhiTest(FAutomationTestBase& Test, const FString& CaseLabel)
	{
		const bool bHasActiveRenderingRhi = HasRenderingRhi();
		UE_LOG(LogTDFluidGpuWaterWetnessRhiTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), *CaseLabel, bHasActiveRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (bHasActiveRenderingRhi)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), *CaseLabel));
		return false;
	}

	void ReportLine(FAutomationTestBase& Test, const FString& Line)
	{
		UE_LOG(LogTDFluidGpuWaterWetnessRhiTests, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
	}

	struct FTDWetnessTestWorld
	{
		FTDWetnessTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidGpuWaterWetnessRhiWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDWetnessTestWorld()
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

		FTDWetnessTestWorld(const FTDWetnessTestWorld&) = delete;
		FTDWetnessTestWorld& operator=(const FTDWetnessTestWorld&) = delete;

		void StepWorld(float DeltaSeconds)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
			ENQUEUE_RENDER_COMMAND(TDFluidGpuWaterWetnessRhiWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	enum class ETDCoastBed : uint8
	{
		Slope,
		Flat
	};

	double ComputeBedHeightCm(ETDCoastBed Bed, double WorldX)
	{
		return Bed == ETDCoastBed::Slope ? WorldX * FMath::Tan(FMath::DegreesToRadians(CoastSlopeDegrees)) : static_cast<double>(FlatFloorTopCm);
	}

	FTDFluidSimulationConfig MakeCoastConfig(int32 Resolution)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = Resolution;
		Config.SimExtentCm = FVector2D(CoastExtentCm, CoastExtentCm);
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = 0.35f;
		Config.PressureIterations = 12;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 1.0f;
		Config.ActorBrushDensityStrength = 0.6f;
		Config.MaxPointsPerTick = 512;
		Config.bEnableWaterLayer = true;
		Config.FlowNoiseAccelerationCmPerSec2 = 0.0f;
		Config.RapidsWhitewaterPerSec = 0.0f;
		Config.WhitewaterFromVorticity = 0.0f;
		Config.RippleWaveSpeedCmPerSec = 0.0f;
		Config.ActorBrushRippleImpulseCm = 0.0f;
		Config.SwashHeightCm = 0.0f;
		Config.BankWetHeightCm = 0.0f;
		Config.WetnessDryingSeconds = DryingSeconds;
		Config.WetnessFromWhitewater = 0.5f;
		Config.CoastWavePeriodSeconds = CoastPeriodSeconds;
		return Config;
	}

	FTDFluidSimulationConfig MakeSmokeConfig(int32 Resolution)
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

	struct FTDCoastScene
	{
		FTDFluidSimulationConfig Config;
		ETDCoastBed Bed = ETDCoastBed::Slope;
		float SeaLevelCm = HighSeaLevelCm;
		float StepSeconds = StepSeconds60;
		int32 AlignedTicks = AlignedTickCount;
	};

	FTDCoastScene MakeCoastScene(int32 Resolution, ETDCoastBed Bed = ETDCoastBed::Slope, int32 AlignedTicks = AlignedTickCount)
	{
		FTDCoastScene Scene;
		Scene.Config = MakeCoastConfig(Resolution);
		Scene.Bed = Bed;
		Scene.AlignedTicks = AlignedTicks;
		return Scene;
	}

	struct FTDGridFrame
	{
		int32 Resolution = 0;
		FVector2D Center = FVector2D::ZeroVector;
		FVector2D Extent = FVector2D::ZeroVector;

		double GetCellSizeCm() const
		{
			return Resolution > 0 ? Extent.X / Resolution : 0.0;
		}

		FVector2D GetCellCenter(int32 CellX, int32 CellY) const
		{
			const double CellSize = GetCellSizeCm();
			return Center - 0.5 * Extent + FVector2D((CellX + 0.5) * CellSize, (CellY + 0.5) * CellSize);
		}

		int32 GetCellIndex(int32 CellX, int32 CellY) const
		{
			return CellY * Resolution + CellX;
		}

		FIntPoint FindCell(const FVector2D& WorldPoint) const
		{
			const double CellSize = GetCellSizeCm();
			const FVector2D Corner = Center - 0.5 * Extent;
			return FIntPoint(FMath::FloorToInt32((WorldPoint.X - Corner.X) / CellSize), FMath::FloorToInt32((WorldPoint.Y - Corner.Y) / CellSize));
		}
	};

	AActor* SpawnSceneAnchor(UWorld& SceneWorld)
	{
		AActor* Anchor = SceneWorld.SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Anchor)
		{
			return nullptr;
		}

		USceneComponent* AnchorRoot = NewObject<USceneComponent>(Anchor);
		Anchor->AddInstanceComponent(AnchorRoot);
		Anchor->SetRootComponent(AnchorRoot);
		AnchorRoot->RegisterComponent();
		Anchor->SetActorLocation(FVector::ZeroVector);
		return Anchor;
	}

	AActor* SpawnBedBlock(UWorld& SceneWorld, UStaticMesh& CubeMesh, ETDCoastBed Bed)
	{
		AActor* BlockActor = SceneWorld.SpawnActor<AActor>();
		if (!BlockActor)
		{
			return nullptr;
		}

		const double PitchRadians = Bed == ETDCoastBed::Slope ? FMath::DegreesToRadians(CoastSlopeDegrees) : 0.0;
		const FRotator Rotation(FMath::RadiansToDegrees(PitchRadians), 0.0, 0.0);
		const FVector Scale = Bed == ETDCoastBed::Slope ? FVector(50.0, 50.0, 1.0) : FVector(100.0, 100.0, 1.0);
		const double TopHeightCm = Bed == ETDCoastBed::Slope ? 0.0 : static_cast<double>(FlatFloorTopCm);
		const FVector Center(0.0, 0.0, TopHeightCm - 50.0 / FMath::Cos(PitchRadians));
		UStaticMeshComponent* BlockMesh = NewObject<UStaticMeshComponent>(BlockActor);
		BlockActor->AddInstanceComponent(BlockMesh);
		BlockActor->SetRootComponent(BlockMesh);
		BlockMesh->SetMobility(EComponentMobility::Movable);
		BlockMesh->SetStaticMesh(&CubeMesh);
		BlockMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		BlockMesh->SetCollisionObjectType(ECC_WorldStatic);
		BlockMesh->SetCollisionResponseToAllChannels(ECR_Block);
		BlockMesh->SetWorldTransform(FTransform(Rotation, Center, Scale));
		BlockMesh->RegisterComponent();
		return BlockActor;
	}

	AActor* SpawnRegionBox(UWorld& SceneWorld, float SurfaceHeightCm)
	{
		constexpr double RegionHalfHeightCm = 50.0;
		constexpr double RegionHalfWidthCm = 5000.0;
		AActor* BoxActor = SceneWorld.SpawnActor<AActor>();
		if (!BoxActor)
		{
			return nullptr;
		}

		UBoxComponent* Box = NewObject<UBoxComponent>(BoxActor);
		BoxActor->AddInstanceComponent(Box);
		BoxActor->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(RegionHalfWidthCm, RegionHalfWidthCm, RegionHalfHeightCm));
		Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Box->RegisterComponent();
		BoxActor->SetActorLocation(FVector(0.0, 0.0, SurfaceHeightCm - RegionHalfHeightCm));
		BoxActor->Tags.Add(GetWaterRegionTag());
		return BoxActor;
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

	ATDFluidWaterActor* SpawnSceneWaterActor(UWorld& SceneWorld, AActor& Anchor, const FTDFluidSimulationConfig& Config, ETDFluidSolverBackend Backend)
	{
		ATDFluidWaterActor* WaterActor = SceneWorld.SpawnActor<ATDFluidWaterActor>(ATDFluidWaterActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (!WaterActor || !WaterActor->LiveComponent)
		{
			return nullptr;
		}

		WaterActor->FollowTargetActor = &Anchor;
		WaterActor->ViewLookAheadCm = 0.0f;
		WaterActor->bSleepWhenNoWaterInWindow = false;
		WaterActor->bSampleTerrainElevation = true;
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(&SceneWorld, ETDFluidPresetKind::River);
		Preset->bOverridePresetDefaults = true;
		Preset->SimulationConfig = Config;
		Preset->SimulationConfig.SolverBackend = Backend;
		WaterActor->InitializeFromPreset(Preset, 0.0f);
		return WaterActor;
	}

	ATDFluidLiveActor* SpawnSmokeActor(UWorld& SceneWorld, const FVector& Location, const FTDFluidSimulationConfig& Config)
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

	struct FTDCoastArm
	{
		FTDWetnessTestWorld* ArmWorld = nullptr;
		const FTDCoastScene* Scene = nullptr;
		ATDFluidWaterActor* WaterActor = nullptr;
		AActor* Anchor = nullptr;
		AActor* RegionActor = nullptr;
		UStaticMeshComponent* SurfaceMesh = nullptr;
		int32 TickCount = 0;

		UTDFluidLiveComponent& GetComponent() const
		{
			return *WaterActor->LiveComponent;
		}

		UMaterialInstanceDynamic* GetSurfaceMaterial() const
		{
			return SurfaceMesh ? Cast<UMaterialInstanceDynamic>(SurfaceMesh->GetMaterial(0)) : nullptr;
		}

		void StepFor(float DeltaSeconds)
		{
			ArmWorld->StepWorld(DeltaSeconds);
			++TickCount;
		}

		void Step()
		{
			StepFor(Scene->StepSeconds);
		}

		void StepMany(int32 StepCount)
		{
			for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
			{
				Step();
			}
		}

		void SetSeaLevel(float SeaLevelCm)
		{
			UWorld& SceneWorld = *ArmWorld->World;
			if (RegionActor)
			{
				SceneWorld.DestroyActor(RegionActor);
			}
			RegionActor = SpawnRegionBox(SceneWorld, SeaLevelCm);
			if (UTDFluidLiveSubsystem* FluidSubsystem = SceneWorld.GetSubsystem<UTDFluidLiveSubsystem>())
			{
				FluidSubsystem->NotifyWaterSurfacesChanged();
			}
		}

		FTDGridFrame DescribeGrid() const
		{
			FTDGridFrame Grid;
			Grid.Resolution = GetComponent().GetSimulationResolution();
			Grid.Center = GetComponent().GetSimulationCenterWorld();
			Grid.Extent = GetComponent().GetSimulationExtentCm();
			return Grid;
		}

		bool CaptureSurface(TArray<FFloat16Color>& OutPixels) const
		{
			if (!GetComponent().IsUsingGpuSolver())
			{
				OutPixels = GetComponent().GetPackedWaterSurfacePixels();
				return !OutPixels.IsEmpty();
			}

			UMaterialInstanceDynamic* SurfaceMaterial = GetSurfaceMaterial();
			return ReadRenderTargetPixels(SurfaceMaterial ? SurfaceMaterial->K2_GetTextureParameterValue(TEXT("WaterSurfaceTexture")) : nullptr, OutPixels);
		}

		bool CaptureDensity(TArray<FFloat16Color>& OutPixels) const
		{
			if (!GetComponent().IsUsingGpuSolver())
			{
				OutPixels = GetComponent().GetPackedDensityPressurePixels();
				return !OutPixels.IsEmpty();
			}

			return ReadRenderTargetPixels(GetComponent().GetDensityPressureTexture(), OutPixels);
		}

		bool CaptureWetness(TArray<float>& OutWetness) const
		{
			TArray<FFloat16Color> Pixels;
			OutWetness.Reset();
			if (!CaptureSurface(Pixels))
			{
				return false;
			}

			OutWetness.Reserve(Pixels.Num());
			for (const FFloat16Color& Pixel : Pixels)
			{
				OutWetness.Add(Pixel.G.GetFloat());
			}
			return true;
		}

		float ReadCoastPhase() const
		{
			UMaterialInstanceDynamic* SurfaceMaterial = GetSurfaceMaterial();
			return SurfaceMaterial ? SurfaceMaterial->K2_GetScalarParameterValue(TEXT("CoastWavePhase")) : -1.0f;
		}
	};

	bool WaitForTerrainSampling(FAutomationTestBase& Test, const FString& ArmLabel, FTDCoastArm& Arm)
	{
		for (int32 TickIndex = 0; TickIndex < TerrainWaitTickLimit; ++TickIndex)
		{
			if (Arm.GetComponent().GetWaterLayerDisplayAlpha() > 0.0f)
			{
				return true;
			}

			Arm.Step();
		}

		if (Arm.GetComponent().GetWaterLayerDisplayAlpha() > 0.0f)
		{
			return true;
		}

		Test.AddError(MakeTerrainTimeoutMessage(ArmLabel));
		return false;
	}

	bool RunCoastArm(FAutomationTestBase& Test, const FString& ArmLabel, const FTDCoastScene& Scene, ETDFluidSolverBackend Backend, TFunctionRef<bool(FTDCoastArm&)> RunScenario)
	{
		FTDWetnessTestWorld ArmTestWorld;
		FTDCoastArm Arm;
		Arm.ArmWorld = &ArmTestWorld;
		Arm.Scene = &Scene;
		UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
		UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath);
		if (!Test.TestTrue(*(ArmLabel + TEXT(" loaded the engine cube mesh and shape material")), CubeMesh != nullptr && ShapeMaterial != nullptr))
		{
			return false;
		}

		UWorld& SceneWorld = *ArmTestWorld.World;
		SpawnBedBlock(SceneWorld, *CubeMesh, Scene.Bed);
		Arm.RegionActor = SpawnRegionBox(SceneWorld, Scene.SeaLevelCm);
		Arm.SurfaceMesh = SpawnSurfaceMesh(SceneWorld, *CubeMesh, *ShapeMaterial);
		Arm.Anchor = SpawnSceneAnchor(SceneWorld);
		Arm.WaterActor = Arm.Anchor ? SpawnSceneWaterActor(SceneWorld, *Arm.Anchor, Scene.Config, Backend) : nullptr;
		if (!Test.TestTrue(*(ArmLabel + TEXT(" spawned the coast scene")), Arm.RegionActor != nullptr && Arm.SurfaceMesh != nullptr && Arm.Anchor != nullptr && Arm.WaterActor != nullptr))
		{
			return false;
		}

		Arm.Step();
		if (Backend == ETDFluidSolverBackend::Gpu && !Arm.GetComponent().IsUsingGpuSolver())
		{
			Test.AddError(MakeGpuNotSelectedMessage(ArmLabel));
			return false;
		}
		if (!WaitForTerrainSampling(Test, ArmLabel, Arm))
		{
			return false;
		}
		if (Arm.TickCount > Scene.AlignedTicks && Scene.AlignedTicks > 0)
		{
			Test.AddError(FString::Printf(TEXT("%s: terrain sampling took %d ticks, past the %d tick alignment point (harness defect)"), *ArmLabel, Arm.TickCount, Scene.AlignedTicks));
			return false;
		}

		while (Arm.TickCount < Scene.AlignedTicks)
		{
			Arm.Step();
		}
		return RunScenario(Arm);
	}

	bool VerifyCoastHarness(FAutomationTestBase& Test, const FString& Label)
	{
		static bool bHasCheckedHarness = false;
		static bool bIsHarnessValid = false;
		if (bHasCheckedHarness)
		{
			if (!bIsHarnessValid)
			{
				Test.AddError(FString::Printf(TEXT("%s: the GPU coast harness is invalid, see the first wetness test that failed it"), *Label));
			}
			return bIsHarnessValid;
		}

		bHasCheckedHarness = true;
		const FTDCoastScene Scene = MakeCoastScene(CoastResolution, ETDCoastBed::Slope, 0);
		const FString HarnessLabel = Label + TEXT(" harness");
		bIsHarnessValid = RunCoastArm(Test, HarnessLabel, Scene, ETDFluidSolverBackend::Gpu, [&Test, &HarnessLabel](FTDCoastArm& Arm) -> bool
		{
			Arm.StepMany(SettleStepCount);
			TArray<FFloat16Color> Pixels;
			const FTDGridFrame Grid = Arm.DescribeGrid();
			if (!Arm.CaptureSurface(Pixels) || Pixels.Num() != Grid.Resolution * Grid.Resolution)
			{
				Test.AddError(FString::Printf(TEXT("%s: the GPU surface output could not be read back"), *HarnessLabel));
				return false;
			}

			int32 DeepCellCount = 0;
			int32 DryCellCount = 0;
			for (const FFloat16Color& Pixel : Pixels)
			{
				DeepCellCount += Pixel.R.GetFloat() > 0.0f ? 1 : 0;
				DryCellCount += Pixel.R.GetFloat() <= 0.0f ? 1 : 0;
			}
			if (DeepCellCount > 0 && DryCellCount > 0)
			{
				return true;
			}

			Test.AddError(FString::Printf(TEXT("%s: GPU coast scene has %d water cells and %d dry cells (harness defect)"), *HarnessLabel, DeepCellCount, DryCellCount));
			return false;
		});
		return bIsHarnessValid;
	}

	bool PrepareWetnessCase(FAutomationTestBase& Test, const FString& Label, const FTDWetnessCaseScopes& Scopes, FTDFluidSimulationConfig& InOutConfig)
	{
		if (!CanRunWetnessRhiTest(Test, Label))
		{
			return false;
		}
		if (!Scopes.WaterGpuScope.IsValid())
		{
			Test.AddError(MakeMissingWaterGpuMessage(Label));
			return false;
		}
		if (!VerifyCoastHarness(Test, Label))
		{
			return false;
		}

		return RequireConfigBool(Test, Label, InOutConfig, EnableWetnessFieldName, true);
	}

	bool RequireWetnessConsoleVariable(FAutomationTestBase& Test, const FString& Label, const FTDWetnessConsoleVariableScope& Scope)
	{
		if (Scope.IsValid())
		{
			return true;
		}

		Test.AddError(MakeMissingConsoleVariableMessage(Label));
		return false;
	}

	enum class ETDLoweredCellKind : uint8
	{
		Skipped,
		StillWet,
		DriedBand,
		NeverWet
	};

	ETDLoweredCellKind ClassifyLoweredCell(double BedHeightCm)
	{
		if (BedHeightCm < LoweredSeaLevelCm - ClassificationMarginCm)
		{
			return ETDLoweredCellKind::StillWet;
		}
		if (BedHeightCm > LoweredSeaLevelCm + ClassificationMarginCm && BedHeightCm < HighSeaLevelCm - ClassificationMarginCm)
		{
			return ETDLoweredCellKind::DriedBand;
		}
		if (BedHeightCm > HighSeaLevelCm + ClassificationMarginCm)
		{
			return ETDLoweredCellKind::NeverWet;
		}
		return ETDLoweredCellKind::Skipped;
	}

	struct FTDFrameComparison
	{
		int32 CheckedCount = 0;
		int32 MismatchCount = 0;
		float LargestDifference = 0.0f;
		FString FirstMismatch;

		void Record(bool bMatches, int32 CellX, int32 CellY, float Actual, float Expected)
		{
			++CheckedCount;
			LargestDifference = FMath::Max(LargestDifference, FMath::Abs(Actual - Expected));
			if (bMatches)
			{
				return;
			}

			if (MismatchCount == 0)
			{
				FirstMismatch = FString::Printf(TEXT("cell (%d,%d) actual %.5f expected %.5f"), CellX, CellY, Actual, Expected);
			}
			++MismatchCount;
		}

		bool Finish(FAutomationTestBase& Test, const FString& Label, const FString& Description) const
		{
			if (CheckedCount == 0)
			{
				Test.AddError(FString::Printf(TEXT("%s: %s: no cells were checked"), *Label, *Description));
				return false;
			}

			UE_LOG(LogTDFluidGpuWaterWetnessRhiTests, Display, TEXT("TDFluidGpuWaterWetness: %s: %s checked=%d mismatches=%d largest=%.5f"), *Label, *Description, CheckedCount, MismatchCount, LargestDifference);
			if (MismatchCount == 0)
			{
				return true;
			}

			Test.AddError(FString::Printf(TEXT("%s: %s: %d of %d cells differ, first %s"), *Label, *Description, MismatchCount, CheckedCount, *FirstMismatch));
			return false;
		}
	};

	bool CompareFrames(FAutomationTestBase& Test, const FString& Label, const FString& Description, const FTDGridFrame& Grid, const TArray<float>& Cpu, const TArray<float>& Gpu, float Tolerance)
	{
		FTDFrameComparison Comparison;
		if (Cpu.Num() != Gpu.Num() || Cpu.Num() != Grid.Resolution * Grid.Resolution)
		{
			Test.AddError(FString::Printf(TEXT("%s: %s: frame sizes differ (cpu %d, gpu %d, grid %d)"), *Label, *Description, Cpu.Num(), Gpu.Num(), Grid.Resolution * Grid.Resolution));
			return false;
		}

		for (int32 CellY = 0; CellY < Grid.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Grid.Resolution; ++CellX)
			{
				const int32 CellIndex = Grid.GetCellIndex(CellX, CellY);
				Comparison.Record(FMath::Abs(Cpu[CellIndex] - Gpu[CellIndex]) <= Tolerance, CellX, CellY, Gpu[CellIndex], Cpu[CellIndex]);
			}
		}
		return Comparison.Finish(Test, Label, Description);
	}

	bool AreGridsAligned(FAutomationTestBase& Test, const FString& Label, const FTDGridFrame& Cpu, const FTDGridFrame& Gpu)
	{
		const bool bAreAligned = Cpu.Resolution == Gpu.Resolution && Cpu.Center.Equals(Gpu.Center, 0.01) && Cpu.Extent.Equals(Gpu.Extent, 0.01);
		return Test.TestTrue(*FString::Printf(TEXT("%s: the CPU and GPU windows have the same resolution, center and extent"), *Label), bAreAligned);
	}

	struct FTDDryingRecord
	{
		FTDGridFrame Grid;
		TArray<int32> CheckpointSteps;
		TArray<TArray<float>> Frames;
	};

	bool RunDryingArm(FAutomationTestBase& Test, const FString& ArmLabel, ETDFluidSolverBackend Backend, const FTDCoastScene& Scene, const TArray<int32>& CheckpointSteps, FTDDryingRecord& OutRecord)
	{
		return RunCoastArm(Test, ArmLabel, Scene, Backend, [&Test, &ArmLabel, &CheckpointSteps, &OutRecord](FTDCoastArm& Arm) -> bool
		{
			Arm.SetSeaLevel(LoweredSeaLevelCm);
			Arm.Step();
			OutRecord.Grid = Arm.DescribeGrid();
			OutRecord.CheckpointSteps = CheckpointSteps;
			OutRecord.Frames.Reset();
			TArray<float>& InitialFrame = OutRecord.Frames.AddDefaulted_GetRef();
			if (!Arm.CaptureWetness(InitialFrame))
			{
				Test.AddError(FString::Printf(TEXT("%s: the surface output could not be read back"), *ArmLabel));
				return false;
			}

			int32 StepsDone = 0;
			for (const int32 CheckpointStep : CheckpointSteps)
			{
				Arm.StepMany(CheckpointStep - StepsDone);
				StepsDone = CheckpointStep;
				TArray<float>& CheckpointFrame = OutRecord.Frames.AddDefaulted_GetRef();
				if (!Arm.CaptureWetness(CheckpointFrame))
				{
					Test.AddError(FString::Printf(TEXT("%s: the surface output could not be read back"), *ArmLabel));
					return false;
				}
			}
			return true;
		});
	}

	bool ExpectDryingRecord(FAutomationTestBase& Test, const FString& Label, const FTDDryingRecord& Record, float StepSeconds, float TimeConstantSeconds, float BandRelativeTolerance)
	{
		bool bAllMatch = true;
		const FTDGridFrame& Grid = Record.Grid;
		const TArray<float>& Initial = Record.Frames[0];
		FTDFrameComparison InitialBand;
		for (int32 CellY = 0; CellY < Grid.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Grid.Resolution; ++CellX)
			{
				const double BedHeightCm = ComputeBedHeightCm(ETDCoastBed::Slope, Grid.GetCellCenter(CellX, CellY).X);
				if (ClassifyLoweredCell(BedHeightCm) != ETDLoweredCellKind::DriedBand)
				{
					continue;
				}

				const float Value = Initial[Grid.GetCellIndex(CellX, CellY)];
				InitialBand.Record(Value >= 0.99f, CellX, CellY, Value, 1.0f);
			}
		}
		bAllMatch &= InitialBand.Finish(Test, Label, TEXT("cells that dried by lowering the surface start near 1 right after the first dry step"));

		for (int32 CheckpointIndex = 0; CheckpointIndex < Record.CheckpointSteps.Num(); ++CheckpointIndex)
		{
			const double ElapsedSeconds = static_cast<double>(Record.CheckpointSteps[CheckpointIndex]) * static_cast<double>(StepSeconds);
			const double ExpectedRetention = FMath::Exp(-ElapsedSeconds / static_cast<double>(TimeConstantSeconds));
			const TArray<float>& Frame = Record.Frames[CheckpointIndex + 1];
			FTDFrameComparison Comparison;
			for (int32 CellY = 0; CellY < Grid.Resolution; ++CellY)
			{
				for (int32 CellX = 0; CellX < Grid.Resolution; ++CellX)
				{
					const int32 CellIndex = Grid.GetCellIndex(CellX, CellY);
					const double BedHeightCm = ComputeBedHeightCm(ETDCoastBed::Slope, Grid.GetCellCenter(CellX, CellY).X);
					const ETDLoweredCellKind Kind = ClassifyLoweredCell(BedHeightCm);
					if (Kind == ETDLoweredCellKind::Skipped)
					{
						continue;
					}

					const float Expected = Kind == ETDLoweredCellKind::StillWet ? 1.0f : (Kind == ETDLoweredCellKind::NeverWet ? 0.0f : Initial[CellIndex] * static_cast<float>(ExpectedRetention));
					const float Tolerance = Kind == ETDLoweredCellKind::DriedBand ? FMath::Max(ParityTolerance, BandRelativeTolerance * Expected) : ParityTolerance;
					Comparison.Record(FMath::Abs(Frame[CellIndex] - Expected) <= Tolerance, CellX, CellY, Frame[CellIndex], Expected);
				}
			}
			bAllMatch &= Comparison.Finish(Test, Label, FString::Printf(TEXT("drying after %d steps matches exp(-%.3f/%.1f)"), Record.CheckpointSteps[CheckpointIndex], ElapsedSeconds, TimeConstantSeconds));
		}
		return bAllMatch;
	}

	struct FTDSwashRecord
	{
		FTDGridFrame Grid;
		TArray<TArray<float>> StepFrames;
		TArray<float> Phases;
		int32 BandCellPeak = 0;
	};

	bool RunSwashArm(FAutomationTestBase& Test, const FString& ArmLabel, ETDFluidSolverBackend Backend, const FTDCoastScene& Scene, FTDSwashRecord& OutRecord)
	{
		return RunCoastArm(Test, ArmLabel, Scene, Backend, [&Test, &ArmLabel, &OutRecord](FTDCoastArm& Arm) -> bool
		{
			OutRecord.Grid = Arm.DescribeGrid();
			OutRecord.StepFrames.Reset();
			OutRecord.Phases.Reset();
			OutRecord.BandCellPeak = 0;
			for (int32 StepIndex = 0; StepIndex < SwashParityStepCount; ++StepIndex)
			{
				Arm.Step();
				OutRecord.Phases.Add(Arm.ReadCoastPhase());
				TArray<float>& Frame = OutRecord.StepFrames.AddDefaulted_GetRef();
				if (!Arm.CaptureWetness(Frame))
				{
					Test.AddError(FString::Printf(TEXT("%s: the surface output could not be read back at step %d"), *ArmLabel, StepIndex));
					return false;
				}

				int32 BandCellCount = 0;
				for (int32 CellY = 0; CellY < OutRecord.Grid.Resolution; CellY += 16)
				{
					for (int32 CellX = 0; CellX < OutRecord.Grid.Resolution; ++CellX)
					{
						const double BedHeightCm = ComputeBedHeightCm(ETDCoastBed::Slope, OutRecord.Grid.GetCellCenter(CellX, CellY).X);
						BandCellCount += (BedHeightCm > ClassificationMarginCm && Frame[OutRecord.Grid.GetCellIndex(CellX, CellY)] > 0.9f) ? 1 : 0;
					}
				}
				OutRecord.BandCellPeak = FMath::Max(OutRecord.BandCellPeak, BandCellCount);
			}
			return true;
		});
	}

	float ComputeSwashReachCm(float SwashHeight, float Phase)
	{
		return SwashHeight * FMath::Max(0.0f, FMath::Sin(UE_TWO_PI * Phase));
	}

	struct FTDSwashEnd
	{
		FTDGridFrame Grid;
		float Phase = -1.0f;
		float ReachCm = 0.0f;
		double MeasuredEndCm = 0.0;
		double ExpectedEndCm = 0.0;
		bool bIsValid = false;
	};

	double ComputeExpectedBandEndCm(const FTDGridFrame& Grid, int32 BlockDivisor, float ReachCm)
	{
		const double CellSize = Grid.GetCellSizeCm();
		const double BlockSize = CellSize * BlockDivisor;
		const double CornerX = Grid.Center.X - 0.5 * Grid.Extent.X;
		const int32 RowY = Grid.Resolution / 2;
		for (int32 CellX = 1; CellX < Grid.Resolution; ++CellX)
		{
			const double CenterX = Grid.GetCellCenter(CellX, RowY).X;
			if (CenterX <= 0.0)
			{
				continue;
			}

			const int32 BlockIndex = FMath::FloorToInt32((CenterX - CornerX) / BlockSize);
			const double BlockCenterX = CornerX + (BlockIndex + 0.5) * BlockSize;
			if (ComputeBedHeightCm(ETDCoastBed::Slope, BlockCenterX) >= ReachCm)
			{
				return 0.5 * (Grid.GetCellCenter(CellX - 1, RowY).X + CenterX);
			}
		}
		return -1.0;
	}

	double MeasureBandEndCm(const FTDGridFrame& Grid, const TArray<float>& Wetness)
	{
		const int32 RowY = Grid.Resolution / 2;
		const double CellSize = Grid.GetCellSizeCm();
		for (int32 CellX = 1; CellX < Grid.Resolution; ++CellX)
		{
			const float Previous = Wetness[Grid.GetCellIndex(CellX - 1, RowY)];
			const float Current = Wetness[Grid.GetCellIndex(CellX, RowY)];
			if (Grid.GetCellCenter(CellX, RowY).X <= 0.0 || Current >= 0.5f || Previous < 0.5f)
			{
				continue;
			}

			const double Fraction = (Previous - 0.5) / FMath::Max(static_cast<double>(Previous - Current), 1.0e-6);
			return Grid.GetCellCenter(CellX - 1, RowY).X + Fraction * CellSize;
		}
		return -1.0;
	}

	bool RunSwashEndArm(FAutomationTestBase& Test, const FString& ArmLabel, ETDFluidSolverBackend Backend, const FTDCoastScene& Scene, int32 BlockDivisor, FTDSwashEnd& OutEnd)
	{
		return RunCoastArm(Test, ArmLabel, Scene, Backend, [&Test, &ArmLabel, BlockDivisor, &Scene, &OutEnd](FTDCoastArm& Arm) -> bool
		{
			constexpr int32 PeakSearchStepLimit = 720;
			OutEnd.Grid = Arm.DescribeGrid();
			for (int32 StepIndex = 0; StepIndex < PeakSearchStepLimit; ++StepIndex)
			{
				Arm.Step();
				const float Phase = Arm.ReadCoastPhase();
				if (Phase < SwashPeakPhase || Phase >= SwashPeakPhase + 0.05f)
				{
					continue;
				}

				TArray<float> Wetness;
				if (!Arm.CaptureWetness(Wetness))
				{
					Test.AddError(FString::Printf(TEXT("%s: the surface output could not be read back"), *ArmLabel));
					return false;
				}

				OutEnd.Phase = Phase;
				OutEnd.ReachCm = ComputeSwashReachCm(Scene.Config.SwashHeightCm, Phase);
				OutEnd.MeasuredEndCm = MeasureBandEndCm(OutEnd.Grid, Wetness);
				OutEnd.ExpectedEndCm = ComputeExpectedBandEndCm(OutEnd.Grid, BlockDivisor, OutEnd.ReachCm);
				OutEnd.bIsValid = OutEnd.MeasuredEndCm > 0.0 && OutEnd.ExpectedEndCm > 0.0;
				return true;
			}

			Test.AddError(FString::Printf(TEXT("%s: the coast wave phase never reached %.2f within %d steps (harness defect)"), *ArmLabel, SwashPeakPhase, PeakSearchStepLimit));
			return false;
		});
	}

	bool ExpectSwashEnd(FAutomationTestBase& Test, const FString& ArmLabel, const FTDSwashEnd& End)
	{
		const double HalfCellCm = 0.5 * End.Grid.GetCellSizeCm();
		ReportLine(Test, FString::Printf(TEXT("TDFluidGpuWaterWetness: %s phase=%.4f reach=%.2f measured_end=%.2f expected_end=%.2f half_cell=%.2f"), *ArmLabel, End.Phase, End.ReachCm, End.MeasuredEndCm, End.ExpectedEndCm, HalfCellCm));
		if (!Test.TestTrue(*(ArmLabel + TEXT(" found a swash band end along the shore normal")), End.bIsValid))
		{
			return false;
		}

		return Test.TestTrue(*FString::Printf(TEXT("%s swash band end %.2f cm is within half a cell of the expected %.2f cm"), *ArmLabel, End.MeasuredEndCm, End.ExpectedEndCm), FMath::Abs(End.MeasuredEndCm - End.ExpectedEndCm) <= HalfCellCm);
	}

	int32 CountPartialCells(const TArray<float>& Wetness)
	{
		int32 PartialCount = 0;
		for (const float Value : Wetness)
		{
			PartialCount += (Value > 0.05f && Value < 0.95f) ? 1 : 0;
		}
		return PartialCount;
	}

	bool AreRawBitsEqual(const TArray<FFloat16Color>& Left, const TArray<FFloat16Color>& Right)
	{
		if (Left.Num() != Right.Num() || Left.IsEmpty())
		{
			return false;
		}

		for (int32 PixelIndex = 0; PixelIndex < Left.Num(); ++PixelIndex)
		{
			if (Left[PixelIndex].G.Encoded != Right[PixelIndex].G.Encoded)
			{
				return false;
			}
		}
		return true;
	}

	struct FTDRunOutput
	{
		TArray<FFloat16Color> Surface;
		FTDGridFrame Grid;
		int32 SmokeGpuActorCount = 0;
	};

	bool RunDeterministicArm(FAutomationTestBase& Test, const FString& ArmLabel, FTDFluidSimulationConfig Config, int32 SmokeCount, FTDRunOutput& OutRun)
	{
		FTDCoastScene Scene = MakeCoastScene(CoastResolution);
		Scene.Config = Config;
		return RunCoastArm(Test, ArmLabel, Scene, ETDFluidSolverBackend::Gpu, [&Test, &ArmLabel, SmokeCount, &OutRun](FTDCoastArm& Arm) -> bool
		{
			TArray<ATDFluidLiveActor*> SmokeActors;
			const int32 SmokeResolutions[SmokeActorCount] = { 128, 192, 256 };
			for (int32 SmokeIndex = 0; SmokeIndex < SmokeCount; ++SmokeIndex)
			{
				SmokeActors.Add(SpawnSmokeActor(*Arm.ArmWorld->World, FVector(20000.0 + 8000.0 * SmokeIndex, 0.0, 0.0), MakeSmokeConfig(SmokeResolutions[SmokeIndex])));
			}

			Arm.SetSeaLevel(LoweredSeaLevelCm);
			for (int32 StepIndex = 0; StepIndex < BatchStepCount; ++StepIndex)
			{
				for (ATDFluidLiveActor* SmokeActor : SmokeActors)
				{
					if (SmokeActor)
					{
						SmokeActor->LiveComponent->QueueBrushStamp(MakeOrbitSmokeStamp(SmokeActor->GetActorLocation(), StepIndex));
					}
				}
				Arm.Step();
			}

			for (const ATDFluidLiveActor* SmokeActor : SmokeActors)
			{
				OutRun.SmokeGpuActorCount += SmokeActor && SmokeActor->LiveComponent->IsUsingGpuSolver() ? 1 : 0;
			}
			OutRun.Grid = Arm.DescribeGrid();
			if (!Arm.CaptureSurface(OutRun.Surface))
			{
				Test.AddError(FString::Printf(TEXT("%s: the surface output could not be read back"), *ArmLabel));
				return false;
			}
			return true;
		});
	}

	struct FTDToggleRecord
	{
		FTDGridFrame Grid;
		TArray<float> BeforeOff;
		TArray<float> AfterOff;
		TArray<float> AfterOn;
	};

	bool RunToggleArm(FAutomationTestBase& Test, const FString& ArmLabel, ETDFluidSolverBackend Backend, const FTDCoastScene& Scene, FTDWetnessConsoleVariableScope& WetnessScope, FTDToggleRecord& OutRecord)
	{
		return RunCoastArm(Test, ArmLabel, Scene, Backend, [&Test, &ArmLabel, &WetnessScope, &OutRecord](FTDCoastArm& Arm) -> bool
		{
			WetnessScope.SetValue(1);
			Arm.SetSeaLevel(LoweredSeaLevelCm);
			Arm.StepMany(TimeConstantSteps60 / 4);
			OutRecord.Grid = Arm.DescribeGrid();
			bool bCaptured = Arm.CaptureWetness(OutRecord.BeforeOff);
			WetnessScope.SetValue(0);
			Arm.Step();
			bCaptured &= Arm.CaptureWetness(OutRecord.AfterOff);
			WetnessScope.SetValue(1);
			Arm.Step();
			bCaptured &= Arm.CaptureWetness(OutRecord.AfterOn);
			if (!bCaptured)
			{
				Test.AddError(FString::Printf(TEXT("%s: the surface output could not be read back"), *ArmLabel));
			}
			return bCaptured;
		});
	}

	struct FTDRetentionRatios
	{
		double Primary = 0.0;
		double Selective = 0.0;
		bool bIsValid = false;
	};

	void SumDensityChannels(const TArray<FFloat16Color>& Pixels, double& OutPrimary, double& OutSelective)
	{
		OutPrimary = 0.0;
		OutSelective = 0.0;
		for (const FFloat16Color& Pixel : Pixels)
		{
			OutPrimary += Pixel.R.GetFloat();
			OutSelective += Pixel.B.GetFloat();
		}
	}

	bool RunRetentionArm(FAutomationTestBase& Test, const FString& ArmLabel, ETDFluidSolverBackend Backend, const FTDCoastScene& Scene, FTDRetentionRatios& OutRatios)
	{
		return RunCoastArm(Test, ArmLabel, Scene, Backend, [&Test, &ArmLabel, &OutRatios](FTDCoastArm& Arm) -> bool
		{
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector::ZeroVector;
			Stamp.RadiusCm = 70.0f;
			Stamp.DensityStrength = 0.8f;
			Stamp.VelocityStrength = 0.0f;
			Stamp.Channel = ETDFluidBrushChannel::ChannelG_Blood;
			Arm.GetComponent().QueueBrushStamp(Stamp);
			Arm.StepMany(2);
			TArray<FFloat16Color> Before;
			TArray<FFloat16Color> After;
			if (!Arm.CaptureDensity(Before))
			{
				Test.AddError(FString::Printf(TEXT("%s: the density output could not be read back"), *ArmLabel));
				return false;
			}

			Arm.StepMany(WhitewaterStepCount);
			if (!Arm.CaptureDensity(After))
			{
				Test.AddError(FString::Printf(TEXT("%s: the density output could not be read back"), *ArmLabel));
				return false;
			}

			double PrimaryBefore = 0.0;
			double SelectiveBefore = 0.0;
			double PrimaryAfter = 0.0;
			double SelectiveAfter = 0.0;
			SumDensityChannels(Before, PrimaryBefore, SelectiveBefore);
			SumDensityChannels(After, PrimaryAfter, SelectiveAfter);
			if (PrimaryBefore <= 0.0 || SelectiveBefore <= 0.0)
			{
				Test.AddError(FString::Printf(TEXT("%s: the stamp left no density to measure (harness defect)"), *ArmLabel));
				return false;
			}

			OutRatios.Primary = PrimaryAfter / PrimaryBefore;
			OutRatios.Selective = SelectiveAfter / SelectiveBefore;
			OutRatios.bIsValid = true;
			return true;
		});
	}

	bool IsDryCellTouchingWater(const FTDGridFrame& Grid, int32 CellX, int32 CellY)
	{
		const double CellSize = Grid.GetCellSizeCm();
		const FVector2D Center = Grid.GetCellCenter(CellX, CellY);
		if (ComputeBedHeightCm(ETDCoastBed::Slope, Center.X) <= HighSeaLevelCm)
		{
			return false;
		}

		const FVector2D Offsets[4] = { FVector2D(-CellSize, 0.0), FVector2D(CellSize, 0.0), FVector2D(0.0, -CellSize), FVector2D(0.0, CellSize) };
		for (const FVector2D& Offset : Offsets)
		{
			if (ComputeBedHeightCm(ETDCoastBed::Slope, Center.X + Offset.X) < HighSeaLevelCm)
			{
				return true;
			}
		}
		return false;
	}

	struct FTDSplashResult
	{
		double MeanWetness = 0.0;
		int32 CellCount = 0;
	};

	bool RunSplashArm(FAutomationTestBase& Test, const FString& ArmLabel, ETDFluidSolverBackend Backend, const FTDCoastScene& Scene, FTDSplashResult& OutResult)
	{
		return RunCoastArm(Test, ArmLabel, Scene, Backend, [&Test, &ArmLabel, &OutResult](FTDCoastArm& Arm) -> bool
		{
			const FTDGridFrame Grid = Arm.DescribeGrid();
			const FVector2D StampCenter = Grid.GetCellCenter(Grid.Resolution / 2 - 1, Grid.Resolution / 2);
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector(StampCenter.X, StampCenter.Y, 0.0);
			Stamp.RadiusCm = SplashRadiusCm;
			Stamp.DensityStrength = 1.0f;
			Stamp.VelocityStrength = 0.0f;
			Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
			Arm.GetComponent().QueueBrushStamp(Stamp);
			Arm.StepMany(SplashMeasureStepCount);
			TArray<float> Wetness;
			if (!Arm.CaptureWetness(Wetness))
			{
				Test.AddError(FString::Printf(TEXT("%s: the surface output could not be read back"), *ArmLabel));
				return false;
			}

			double Sum = 0.0;
			for (int32 CellY = 0; CellY < Grid.Resolution; ++CellY)
			{
				for (int32 CellX = 0; CellX < Grid.Resolution; ++CellX)
				{
					if (!IsDryCellTouchingWater(Grid, CellX, CellY) || (Grid.GetCellCenter(CellX, CellY) - StampCenter).Size() > SplashRadiusCm)
					{
						continue;
					}

					Sum += Wetness[Grid.GetCellIndex(CellX, CellY)];
					++OutResult.CellCount;
				}
			}
			OutResult.MeanWetness = OutResult.CellCount > 0 ? Sum / OutResult.CellCount : 0.0;
			return OutResult.CellCount > 0;
		});
	}

	struct FTDBankRecord
	{
		FTDGridFrame Grid;
		TArray<float> Wetness;
	};

	bool RunBankArm(FAutomationTestBase& Test, const FString& ArmLabel, ETDFluidSolverBackend Backend, const FTDCoastScene& Scene, FTDBankRecord& OutRecord)
	{
		return RunCoastArm(Test, ArmLabel, Scene, Backend, [&Test, &ArmLabel, &OutRecord](FTDCoastArm& Arm) -> bool
		{
			Arm.StepMany(SettleStepCount);
			OutRecord.Grid = Arm.DescribeGrid();
			if (Arm.CaptureWetness(OutRecord.Wetness))
			{
				return true;
			}

			Test.AddError(FString::Printf(TEXT("%s: the surface output could not be read back"), *ArmLabel));
			return false;
		});
	}

	bool ExpectBankShape(FAutomationTestBase& Test, const FString& Label, const FTDBankRecord& Record)
	{
		FTDFrameComparison Comparison;
		int32 BandCellCount = 0;
		for (int32 CellY = 0; CellY < Record.Grid.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Record.Grid.Resolution; ++CellX)
			{
				const double BedHeightCm = ComputeBedHeightCm(ETDCoastBed::Slope, Record.Grid.GetCellCenter(CellX, CellY).X);
				const float Value = Record.Wetness[Record.Grid.GetCellIndex(CellX, CellY)];
				if (BedHeightCm < HighSeaLevelCm - ClassificationMarginCm)
				{
					Comparison.Record(FMath::Abs(Value - 1.0f) <= ShapeTolerance, CellX, CellY, Value, 1.0f);
					continue;
				}
				if (BedHeightCm > BankHeightCm + ClassificationMarginCm)
				{
					Comparison.Record(FMath::Abs(Value) <= ShapeTolerance, CellX, CellY, Value, 0.0f);
					continue;
				}
				if (BedHeightCm > ClassificationMarginCm && BedHeightCm < BankHeightCm - ClassificationMarginCm)
				{
					const float Expected = 1.0f - static_cast<float>(BedHeightCm) / BankHeightCm;
					Comparison.Record(FMath::Abs(Value - Expected) <= ShapeTolerance, CellX, CellY, Value, Expected);
					++BandCellCount;
				}
			}
		}
		return Comparison.Finish(Test, Label, TEXT("bank band wetness is 1 - height / 10 cm above the water and 0 beyond it")) && Test.TestTrue(*(Label + TEXT(" has bank band cells")), BandCellCount > 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterWetnessDryingParityTest,
	"TDGame.FluidFXRhi.Water.Wetness.DryingParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterWetnessDryingParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterWetnessRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Wetness.DryingParity");
	FTDWetnessCaseScopes Scopes;
	FTDCoastScene Scene = MakeCoastScene(CoastResolution);
	if (!PrepareWetnessCase(*this, CaseLabel, Scopes, Scene.Config))
	{
		return false;
	}

	const TArray<int32> Checkpoints = { TimeConstantSteps60, ThreeTimeConstantSteps60 };
	FTDDryingRecord CpuRecord;
	FTDDryingRecord GpuRecord;
	if (!RunDryingArm(*this, CaseLabel + TEXT(" CPU 192"), ETDFluidSolverBackend::Cpu, Scene, Checkpoints, CpuRecord) || !RunDryingArm(*this, CaseLabel + TEXT(" GPU 192"), ETDFluidSolverBackend::Gpu, Scene, Checkpoints, GpuRecord))
	{
		return false;
	}

	AreGridsAligned(*this, CaseLabel, CpuRecord.Grid, GpuRecord.Grid);
	ExpectDryingRecord(*this, CaseLabel + TEXT(" CPU 192"), CpuRecord, StepSeconds60, DryingSeconds, 0.0f);
	ExpectDryingRecord(*this, CaseLabel + TEXT(" GPU 192"), GpuRecord, StepSeconds60, DryingSeconds, 0.0f);
	for (int32 FrameIndex = 0; FrameIndex < CpuRecord.Frames.Num(); ++FrameIndex)
	{
		const int32 StepCount = FrameIndex == 0 ? 0 : CpuRecord.CheckpointSteps[FrameIndex - 1];
		CompareFrames(*this, CaseLabel, FString::Printf(TEXT("GPU wetness matches the CPU %d steps after the first dry step"), StepCount), GpuRecord.Grid, CpuRecord.Frames[FrameIndex], GpuRecord.Frames[FrameIndex], ParityTolerance);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterWetnessSwashBandParityTest,
	"TDGame.FluidFXRhi.Water.Wetness.SwashBandParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterWetnessSwashBandParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterWetnessRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Wetness.SwashBandParity");
	FTDWetnessCaseScopes Scopes;
	FTDCoastScene Scene = MakeCoastScene(CoastResolution);
	if (!PrepareWetnessCase(*this, CaseLabel, Scopes, Scene.Config))
	{
		return false;
	}

	Scene.Config.SwashHeightCm = SwashHeightCm;
	FTDSwashRecord CpuRecord;
	FTDSwashRecord GpuRecord;
	if (!RunSwashArm(*this, CaseLabel + TEXT(" CPU 192"), ETDFluidSolverBackend::Cpu, Scene, CpuRecord) || !RunSwashArm(*this, CaseLabel + TEXT(" GPU 192"), ETDFluidSolverBackend::Gpu, Scene, GpuRecord))
	{
		return false;
	}

	AreGridsAligned(*this, CaseLabel, CpuRecord.Grid, GpuRecord.Grid);
	TestTrue(*(CaseLabel + TEXT(" the swash wets dry cells above the water in the CPU arm")), CpuRecord.BandCellPeak > 0);
	TestTrue(*(CaseLabel + TEXT(" the swash wets dry cells above the water in the GPU arm")), GpuRecord.BandCellPeak > 0);
	int32 PhaseMismatchCount = 0;
	int32 FrameMismatchCount = 0;
	int32 FirstMismatchStep = INDEX_NONE;
	for (int32 StepIndex = 0; StepIndex < SwashParityStepCount; ++StepIndex)
	{
		PhaseMismatchCount += CpuRecord.Phases[StepIndex] == GpuRecord.Phases[StepIndex] ? 0 : 1;
		bool bFrameMatches = true;
		for (int32 CellIndex = 0; CellIndex < CpuRecord.StepFrames[StepIndex].Num() && bFrameMatches; ++CellIndex)
		{
			bFrameMatches = FMath::Abs(CpuRecord.StepFrames[StepIndex][CellIndex] - GpuRecord.StepFrames[StepIndex][CellIndex]) <= ParityTolerance;
		}
		if (!bFrameMatches)
		{
			FirstMismatchStep = FirstMismatchStep == INDEX_NONE ? StepIndex : FirstMismatchStep;
			++FrameMismatchCount;
		}
	}

	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterWetness: %s steps=%d phase_mismatches=%d frame_mismatches=%d first_mismatch_step=%d"), *CaseLabel, SwashParityStepCount, PhaseMismatchCount, FrameMismatchCount, FirstMismatchStep));
	TestEqual(*(CaseLabel + TEXT(" steps whose CoastWavePhase differs between the CPU and GPU")), PhaseMismatchCount, 0);
	TestEqual(*(CaseLabel + TEXT(" steps whose wetness differs from the CPU by more than 2e-3 in some cell")), FrameMismatchCount, 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterWetnessBankBandParityTest,
	"TDGame.FluidFXRhi.Water.Wetness.BankBandParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterWetnessBankBandParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterWetnessRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Wetness.BankBandParity");
	FTDWetnessCaseScopes Scopes;
	FTDCoastScene Scene = MakeCoastScene(CoastResolution);
	if (!PrepareWetnessCase(*this, CaseLabel, Scopes, Scene.Config))
	{
		return false;
	}

	Scene.Config.BankWetHeightCm = BankHeightCm;
	FTDBankRecord CpuRecord;
	FTDBankRecord GpuRecord;
	if (!RunBankArm(*this, CaseLabel + TEXT(" CPU 192"), ETDFluidSolverBackend::Cpu, Scene, CpuRecord) || !RunBankArm(*this, CaseLabel + TEXT(" GPU 192"), ETDFluidSolverBackend::Gpu, Scene, GpuRecord))
	{
		return false;
	}

	AreGridsAligned(*this, CaseLabel, CpuRecord.Grid, GpuRecord.Grid);
	ExpectBankShape(*this, CaseLabel + TEXT(" CPU 192"), CpuRecord);
	ExpectBankShape(*this, CaseLabel + TEXT(" GPU 192"), GpuRecord);
	CompareFrames(*this, CaseLabel, TEXT("GPU bank band wetness matches the CPU"), GpuRecord.Grid, CpuRecord.Wetness, GpuRecord.Wetness, ParityTolerance);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterWetnessSlidingWindowParityTest,
	"TDGame.FluidFXRhi.Water.Wetness.SlidingWindowParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterWetnessSlidingWindowParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterWetnessRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Wetness.SlidingWindowParity");
	FTDWetnessCaseScopes Scopes;
	FTDCoastScene Scene = MakeCoastScene(CoastResolution);
	if (!PrepareWetnessCase(*this, CaseLabel, Scopes, Scene.Config))
	{
		return false;
	}

	const auto RunSlideArm = [this, &Scene](const FString& ArmLabel, ETDFluidSolverBackend Backend, FTDBankRecord& OutRecord)
	{
		return RunCoastArm(*this, ArmLabel, Scene, Backend, [this, &ArmLabel, &OutRecord](FTDCoastArm& Arm) -> bool
		{
			Arm.SetSeaLevel(LoweredSeaLevelCm);
			Arm.StepMany(TimeConstantSteps60 / 4);
			const double SlideCm = SlideCellCount * Arm.DescribeGrid().GetCellSizeCm();
			for (int32 SlideIndex = 1; SlideIndex <= SlideCount; ++SlideIndex)
			{
				Arm.Anchor->SetActorLocation(FVector(SlideCm * SlideIndex, 0.0, 0.0));
				Arm.StepMany(SlideSettleStepCount);
			}

			Arm.StepMany(TimeConstantSteps60 / 8);
			OutRecord.Grid = Arm.DescribeGrid();
			if (Arm.CaptureWetness(OutRecord.Wetness))
			{
				return true;
			}

			AddError(FString::Printf(TEXT("%s: the surface output could not be read back"), *ArmLabel));
			return false;
		});
	};

	FTDBankRecord CpuRecord;
	FTDBankRecord GpuRecord;
	if (!RunSlideArm(CaseLabel + TEXT(" CPU 192"), ETDFluidSolverBackend::Cpu, CpuRecord) || !RunSlideArm(CaseLabel + TEXT(" GPU 192"), ETDFluidSolverBackend::Gpu, GpuRecord))
	{
		return false;
	}

	const double ExpectedCenterX = SlideCount * SlideCellCount * CpuRecord.Grid.GetCellSizeCm();
	TestTrue(*(CaseLabel + TEXT(" the CPU window followed the pawn through three slides")), FMath::Abs(CpuRecord.Grid.Center.X - ExpectedCenterX) <= CpuRecord.Grid.GetCellSizeCm());
	AreGridsAligned(*this, CaseLabel, CpuRecord.Grid, GpuRecord.Grid);
	TestTrue(*(CaseLabel + TEXT(" the CPU arm carries a partly dried band across the slides")), CountPartialCells(CpuRecord.Wetness) >= 20);
	TestTrue(*(CaseLabel + TEXT(" the GPU arm carries a partly dried band across the slides")), CountPartialCells(GpuRecord.Wetness) >= 20);
	CompareFrames(*this, CaseLabel, TEXT("GPU wetness after three window slides matches the CPU"), GpuRecord.Grid, CpuRecord.Wetness, GpuRecord.Wetness, ParityTolerance);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterWetnessProbeMatchesOutputTest,
	"TDGame.FluidFXRhi.SoloWater.WetnessProbeMatchesOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterWetnessProbeMatchesOutputTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterWetnessRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.SoloWater.WetnessProbeMatchesOutput");
	FTDWetnessCaseScopes Scopes;
	FTDCoastScene Scene = MakeCoastScene(CoastResolution);
	if (!PrepareWetnessCase(*this, CaseLabel, Scopes, Scene.Config))
	{
		return false;
	}

	FTDWetnessConsoleVariableScope WetnessScope(WetnessConsoleVariableName, 1);
	if (!RequireWetnessConsoleVariable(*this, CaseLabel, WetnessScope))
	{
		return false;
	}

	Scene.Config.BankWetHeightCm = BankHeightCm;
	Scene.AlignedTicks = 0;
	const double ProbeXs[] = { -300.0, -9.375, 9.375, 28.125, 46.875, 65.625, 84.375, 103.125, 300.0 };
	return RunCoastArm(*this, CaseLabel + TEXT(" GPU 192"), Scene, ETDFluidSolverBackend::Gpu, [this, &CaseLabel, &ProbeXs, &WetnessScope](FTDCoastArm& Arm) -> bool
	{
		constexpr int32 ArrivalFrameLimit = 12;
		constexpr int32 SettleFrameCount = 8;
		TArray<FVector> ProbePoints;
		for (const double ProbeX : ProbeXs)
		{
			ProbePoints.Add(FVector(ProbeX, 9.375, 0.0));
		}

		Arm.StepMany(SettleFrameCount);
		Arm.GetComponent().SetWaterProbeLocations(ProbePoints);
		TArray<FTDFluidWaterPointSample> Samples;
		for (int32 FrameIndex = 0; FrameIndex < ArrivalFrameLimit; ++FrameIndex)
		{
			Arm.Step();
			if (Arm.GetComponent().GetWaterProbeSamples(Samples) && Samples.Num() == ProbePoints.Num())
			{
				break;
			}
		}
		Arm.StepMany(SettleFrameCount);

		const auto CompareProbesWithOutput = [this, &CaseLabel, &Arm, &ProbePoints, &Samples](const FString& PhaseLabel, bool bExpectsZero) -> bool
		{
			TArray<FFloat16Color> Pixels;
			if (!Arm.GetComponent().GetWaterProbeSamples(Samples) || Samples.Num() != ProbePoints.Num() || !Arm.CaptureSurface(Pixels))
			{
				AddError(FString::Printf(TEXT("%s %s: probe samples or the surface output are not available"), *CaseLabel, *PhaseLabel));
				return false;
			}

			const FTDGridFrame Grid = Arm.DescribeGrid();
			FTDFrameComparison Comparison;
			int32 FullCount = 0;
			int32 PartialCount = 0;
			int32 ZeroCount = 0;
			for (int32 ProbeIndex = 0; ProbeIndex < ProbePoints.Num(); ++ProbeIndex)
			{
				const FIntPoint Cell = Grid.FindCell(FVector2D(ProbePoints[ProbeIndex].X, ProbePoints[ProbeIndex].Y));
				const float OutputValue = Pixels[Grid.GetCellIndex(Cell.X, Cell.Y)].G.GetFloat();
				const float ProbeValue = Samples[ProbeIndex].Wetness;
				const float Expected = bExpectsZero ? 0.0f : OutputValue;
				Comparison.Record(Samples[ProbeIndex].bIsInsideWindow && FMath::Abs(ProbeValue - Expected) <= ProbeTolerance, Cell.X, Cell.Y, ProbeValue, Expected);
				FullCount += OutputValue >= 0.95f ? 1 : 0;
				PartialCount += (OutputValue > 0.05f && OutputValue < 0.95f) ? 1 : 0;
				ZeroCount += OutputValue <= 0.05f ? 1 : 0;
			}

			const bool bMatches = Comparison.Finish(*this, CaseLabel, PhaseLabel);
			if (bExpectsZero)
			{
				return bMatches && TestEqual(*FString::Printf(TEXT("%s %s: probes with a nonzero output wetness"), *CaseLabel, *PhaseLabel), FullCount + PartialCount, 0);
			}

			return bMatches && TestTrue(*FString::Printf(TEXT("%s %s: the probes cover wet, bank band and dry cells"), *CaseLabel, *PhaseLabel), FullCount > 0 && PartialCount > 0 && ZeroCount > 0);
		};

		bool bAllMatch = CompareProbesWithOutput(TEXT("probe wetness equals the output green channel"), false);
		WetnessScope.SetValue(0);
		Arm.StepMany(ArrivalFrameLimit);
		bAllMatch &= CompareProbesWithOutput(TEXT("probe wetness and output are zero with the console variable at 0"), true);
		return bAllMatch;
	}) && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterWetnessSplashWetsBankStatisticallyTest,
	"TDGame.FluidFXRhi.Water.Wetness.SplashWetsBankStatistically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterWetnessSplashWetsBankStatisticallyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterWetnessRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Wetness.SplashWetsBankStatistically");
	FTDWetnessCaseScopes Scopes;
	FTDCoastScene Scene = MakeCoastScene(CoastResolution);
	if (!PrepareWetnessCase(*this, CaseLabel, Scopes, Scene.Config))
	{
		return false;
	}

	FTDSplashResult CpuResult;
	FTDSplashResult GpuResult;
	if (!RunSplashArm(*this, CaseLabel + TEXT(" CPU 192"), ETDFluidSolverBackend::Cpu, Scene, CpuResult) || !RunSplashArm(*this, CaseLabel + TEXT(" GPU 192"), ETDFluidSolverBackend::Gpu, Scene, GpuResult))
	{
		return false;
	}

	const double RelativeDifference = CpuResult.MeanWetness > 0.0 ? FMath::Abs(GpuResult.MeanWetness - CpuResult.MeanWetness) / CpuResult.MeanWetness : 1.0;
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterWetness: %s cpu_cells=%d cpu_mean=%.4f gpu_cells=%d gpu_mean=%.4f relative_difference=%.4f"), *CaseLabel, CpuResult.CellCount, CpuResult.MeanWetness, GpuResult.CellCount, GpuResult.MeanWetness, RelativeDifference));
	TestEqual(*(CaseLabel + TEXT(" cells in the splash region of interest")), GpuResult.CellCount, CpuResult.CellCount);
	TestTrue(*(CaseLabel + TEXT(" CPU mean wetness beside the splash is above 0.2")), CpuResult.MeanWetness > 0.2);
	TestTrue(*(CaseLabel + TEXT(" GPU mean wetness beside the splash is above 0.2")), GpuResult.MeanWetness > 0.2);
	TestTrue(*(CaseLabel + TEXT(" GPU mean wetness is within 10 percent of the CPU")), RelativeDifference <= 0.10);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterWetnessGpu1024MatchesCpu192Test,
	"TDGame.FluidFXRhi.Water.Wetness.Gpu1024MatchesCpu192",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterWetnessGpu1024MatchesCpu192Test::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterWetnessRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Wetness.Gpu1024MatchesCpu192");
	FTDWetnessCaseScopes Scopes;
	FTDCoastScene CpuScene = MakeCoastScene(CoastResolution, ETDCoastBed::Slope, 0);
	if (!PrepareWetnessCase(*this, CaseLabel, Scopes, CpuScene.Config))
	{
		return false;
	}

	FTDCoastScene GpuScene = MakeCoastScene(LargeCoastResolution, ETDCoastBed::Slope, 0);
	RequireConfigBool(*this, CaseLabel, GpuScene.Config, EnableWetnessFieldName, true);
	const TArray<int32> Checkpoints = { TimeConstantSteps60 };
	FTDDryingRecord CpuDrying;
	FTDDryingRecord GpuDrying;
	if (!RunDryingArm(*this, CaseLabel + TEXT(" CPU 192"), ETDFluidSolverBackend::Cpu, CpuScene, Checkpoints, CpuDrying) || !RunDryingArm(*this, CaseLabel + TEXT(" GPU 1024"), ETDFluidSolverBackend::Gpu, GpuScene, Checkpoints, GpuDrying))
	{
		return false;
	}

	ExpectDryingRecord(*this, CaseLabel + TEXT(" CPU 192"), CpuDrying, StepSeconds60, DryingSeconds, 0.0f);
	ExpectDryingRecord(*this, CaseLabel + TEXT(" GPU 1024"), GpuDrying, StepSeconds60, DryingSeconds, RelativeDryingTolerance);

	CpuScene.Config.SwashHeightCm = SwashHeightCm;
	GpuScene.Config.SwashHeightCm = SwashHeightCm;
	FTDSwashEnd CpuEnd;
	FTDSwashEnd GpuEnd;
	if (!RunSwashEndArm(*this, CaseLabel + TEXT(" CPU 192 swash"), ETDFluidSolverBackend::Cpu, CpuScene, 1, CpuEnd) || !RunSwashEndArm(*this, CaseLabel + TEXT(" GPU 1024 swash"), ETDFluidSolverBackend::Gpu, GpuScene, LargeFieldDivisor, GpuEnd))
	{
		return false;
	}

	ExpectSwashEnd(*this, CaseLabel + TEXT(" CPU 192"), CpuEnd);
	ExpectSwashEnd(*this, CaseLabel + TEXT(" GPU 1024"), GpuEnd);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterWetnessHighFrameRateDoesNotStallTest,
	"TDGame.FluidFXRhi.Water.Wetness.HighFrameRateDoesNotStall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterWetnessHighFrameRateDoesNotStallTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterWetnessRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Wetness.HighFrameRateDoesNotStall");
	FTDWetnessCaseScopes Scopes;
	FTDCoastScene Scene = MakeCoastScene(CoastResolution, ETDCoastBed::Slope, 0);
	if (!PrepareWetnessCase(*this, CaseLabel, Scopes, Scene.Config))
	{
		return false;
	}

	Scene.Config.WetnessDryingSeconds = SlowDryingSeconds;
	Scene.StepSeconds = StepSeconds240;
	const TArray<int32> Checkpoints = { HighFrameRateStepCount };
	FTDDryingRecord GpuRecord;
	if (!RunDryingArm(*this, CaseLabel + TEXT(" GPU 192 240 Hz"), ETDFluidSolverBackend::Gpu, Scene, Checkpoints, GpuRecord))
	{
		return false;
	}

	ExpectDryingRecord(*this, CaseLabel + TEXT(" GPU 192 240 Hz"), GpuRecord, StepSeconds240, SlowDryingSeconds, 0.0f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterWetnessBatchedWithSmokeKeepsWetnessTest,
	"TDGame.FluidFXRhi.Water.Wetness.BatchedWithSmokeKeepsWetness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterWetnessBatchedWithSmokeKeepsWetnessTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterWetnessRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Wetness.BatchedWithSmokeKeepsWetness");
	FTDWetnessCaseScopes Scopes;
	FTDCoastScene Scene = MakeCoastScene(CoastResolution);
	if (!PrepareWetnessCase(*this, CaseLabel, Scopes, Scene.Config))
	{
		return false;
	}

	Scene.Config.SwashHeightCm = SwashHeightCm;
	FTDRunOutput SoloRun;
	FTDRunOutput BatchedRun;
	if (!RunDeterministicArm(*this, CaseLabel + TEXT(" GPU 192 solo"), Scene.Config, 0, SoloRun) || !RunDeterministicArm(*this, CaseLabel + TEXT(" GPU 192 batched with smoke"), Scene.Config, SmokeActorCount, BatchedRun))
	{
		return false;
	}

	TArray<float> SoloWetness;
	for (const FFloat16Color& Pixel : SoloRun.Surface)
	{
		SoloWetness.Add(Pixel.G.GetFloat());
	}
	TestEqual(*(CaseLabel + TEXT(" smoke actors running on the GPU beside the water actor")), BatchedRun.SmokeGpuActorCount, SmokeActorCount);
	TestTrue(*(CaseLabel + TEXT(" the solo run has partly dried cells")), CountPartialCells(SoloWetness) >= 20);
	TestTrue(*(CaseLabel + TEXT(" wetness is bit-identical to the solo run")), AreRawBitsEqual(SoloRun.Surface, BatchedRun.Surface));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterWetnessConsoleToggleMatchesCpuTest,
	"TDGame.FluidFXRhi.Water.Wetness.ConsoleToggleMatchesCpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterWetnessConsoleToggleMatchesCpuTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterWetnessRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Wetness.ConsoleToggleMatchesCpu");
	FTDWetnessCaseScopes Scopes;
	FTDCoastScene Scene = MakeCoastScene(CoastResolution);
	if (!PrepareWetnessCase(*this, CaseLabel, Scopes, Scene.Config))
	{
		return false;
	}

	FTDWetnessConsoleVariableScope WetnessScope(WetnessConsoleVariableName, 1);
	if (!RequireWetnessConsoleVariable(*this, CaseLabel, WetnessScope))
	{
		return false;
	}

	FTDToggleRecord CpuRecord;
	FTDToggleRecord GpuRecord;
	if (!RunToggleArm(*this, CaseLabel + TEXT(" CPU 192"), ETDFluidSolverBackend::Cpu, Scene, WetnessScope, CpuRecord) || !RunToggleArm(*this, CaseLabel + TEXT(" GPU 192"), ETDFluidSolverBackend::Gpu, Scene, WetnessScope, GpuRecord))
	{
		return false;
	}

	AreGridsAligned(*this, CaseLabel, CpuRecord.Grid, GpuRecord.Grid);
	TestTrue(*(CaseLabel + TEXT(" the CPU arm has partly dried cells before the switch")), CountPartialCells(CpuRecord.BeforeOff) >= 20);
	TestTrue(*(CaseLabel + TEXT(" the GPU arm has partly dried cells before the switch")), CountPartialCells(GpuRecord.BeforeOff) >= 20);
	for (const bool bIsGpu : { false, true })
	{
		const FTDToggleRecord& Record = bIsGpu ? GpuRecord : CpuRecord;
		const FString ArmLabel = CaseLabel + (bIsGpu ? TEXT(" GPU 192") : TEXT(" CPU 192"));
		FTDFrameComparison OffComparison;
		FTDFrameComparison OnComparison;
		for (int32 CellY = 0; CellY < Record.Grid.Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Record.Grid.Resolution; ++CellX)
			{
				const int32 CellIndex = Record.Grid.GetCellIndex(CellX, CellY);
				OffComparison.Record(FMath::Abs(Record.AfterOff[CellIndex]) <= ParityTolerance, CellX, CellY, Record.AfterOff[CellIndex], 0.0f);
				const ETDLoweredCellKind Kind = ClassifyLoweredCell(ComputeBedHeightCm(ETDCoastBed::Slope, Record.Grid.GetCellCenter(CellX, CellY).X));
				if (Kind == ETDLoweredCellKind::Skipped)
				{
					continue;
				}

				const float Expected = Kind == ETDLoweredCellKind::StillWet ? 1.0f : 0.0f;
				OnComparison.Record(FMath::Abs(Record.AfterOn[CellIndex] - Expected) <= ParityTolerance, CellX, CellY, Record.AfterOn[CellIndex], Expected);
			}
		}
		OffComparison.Finish(*this, ArmLabel, TEXT("wetness output is zero everywhere one step after the switch to 0"));
		OnComparison.Finish(*this, ArmLabel, TEXT("the first step after the switch back to 1 restarts from the instantaneous source"));
	}

	CompareFrames(*this, CaseLabel, TEXT("GPU wetness after the switch back to 1 matches the CPU"), GpuRecord.Grid, CpuRecord.AfterOn, GpuRecord.AfterOn, ParityTolerance);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterWetnessWhitewaterDissipationParityTest,
	"TDGame.FluidFXRhi.Water.Wetness.WhitewaterDissipationParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterWetnessWhitewaterDissipationParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterWetnessRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Wetness.WhitewaterDissipationParity");
	FTDWetnessCaseScopes Scopes;
	FTDCoastScene Scene = MakeCoastScene(CoastResolution, ETDCoastBed::Flat);
	if (!PrepareWetnessCase(*this, CaseLabel, Scopes, Scene.Config))
	{
		return false;
	}

	if (!RequireConfigBool(*this, CaseLabel, Scene.Config, WhitewaterOverrideFieldName, true) || !RequireConfigFloat(*this, CaseLabel, Scene.Config, WhitewaterRateFieldName, static_cast<float>(LaceRetentionSeaDissipation)))
	{
		return false;
	}

	FTDRetentionRatios CpuRatios;
	FTDRetentionRatios GpuRatios;
	if (!RunRetentionArm(*this, CaseLabel + TEXT(" CPU 192"), ETDFluidSolverBackend::Cpu, Scene, CpuRatios) || !RunRetentionArm(*this, CaseLabel + TEXT(" GPU 192"), ETDFluidSolverBackend::Gpu, Scene, GpuRatios))
	{
		return false;
	}

	const double ExpectedPrimary = FMath::Pow(1.0 / (1.0 + LaceRetentionSeaDissipation * static_cast<double>(StepSeconds60)), static_cast<double>(WhitewaterStepCount));
	const double PrimaryRelativeDifference = CpuRatios.Primary > 0.0 ? FMath::Abs(GpuRatios.Primary - CpuRatios.Primary) / CpuRatios.Primary : 1.0;
	ReportLine(*this, FString::Printf(TEXT("TDFluidGpuWaterWetness: %s cpu_primary=%.5f gpu_primary=%.5f expected_primary=%.5f cpu_selective=%.5f gpu_selective=%.5f expected_selective=%.5f"), *CaseLabel, CpuRatios.Primary, GpuRatios.Primary, ExpectedPrimary, CpuRatios.Selective, GpuRatios.Selective, SelectiveRetention));
	TestTrue(*(CaseLabel + TEXT(" GPU five second whitewater ratio is within 1 percent of the CPU")), PrimaryRelativeDifference <= WhitewaterRetentionTolerance);
	TestTrue(*(CaseLabel + TEXT(" CPU five second whitewater ratio follows the override rate")), FMath::Abs(CpuRatios.Primary / ExpectedPrimary - 1.0) <= WhitewaterRetentionTolerance);
	TestTrue(*(CaseLabel + TEXT(" GPU five second whitewater ratio follows the override rate")), FMath::Abs(GpuRatios.Primary / ExpectedPrimary - 1.0) <= WhitewaterRetentionTolerance);
	TestTrue(*(CaseLabel + TEXT(" CPU selective density keeps the existing dissipation")), FMath::Abs(CpuRatios.Selective / SelectiveRetention - 1.0) <= WhitewaterRetentionTolerance);
	TestTrue(*(CaseLabel + TEXT(" GPU selective density keeps the existing dissipation")), FMath::Abs(GpuRatios.Selective / SelectiveRetention - 1.0) <= WhitewaterRetentionTolerance);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterWetnessRunToRunDeterministicTest,
	"TDGame.FluidFXRhi.Water.Wetness.RunToRunDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterWetnessRunToRunDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterWetnessRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.Water.Wetness.RunToRunDeterministic");
	FTDWetnessCaseScopes Scopes;
	FTDCoastScene Scene = MakeCoastScene(CoastResolution);
	if (!PrepareWetnessCase(*this, CaseLabel, Scopes, Scene.Config))
	{
		return false;
	}

	Scene.Config.SwashHeightCm = SwashHeightCm;
	Scene.Config.BankWetHeightCm = BankHeightCm;
	FTDRunOutput Runs[2];
	for (int32 RunIndex = 0; RunIndex < 2; ++RunIndex)
	{
		if (!RunDeterministicArm(*this, FString::Printf(TEXT("%s GPU 192 run %d"), *CaseLabel, RunIndex + 1), Scene.Config, 0, Runs[RunIndex]))
		{
			return false;
		}
	}

	TArray<float> FirstWetness;
	for (const FFloat16Color& Pixel : Runs[0].Surface)
	{
		FirstWetness.Add(Pixel.G.GetFloat());
	}
	TestTrue(*(CaseLabel + TEXT(" the first run has partly dried cells")), CountPartialCells(FirstWetness) >= 20);
	TestTrue(*(CaseLabel + TEXT(" wetness is bit-identical across runs")), AreRawBitsEqual(Runs[0].Surface, Runs[1].Surface));
	return !HasAnyErrors();
}

#endif
