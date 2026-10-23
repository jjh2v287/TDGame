#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidRiverActor.h"
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
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidGpuWaterSoloRhiTests, Log, All);

namespace TDFluidGpuWaterSoloRhiTestPrivate
{
	constexpr float SoloStepSeconds = 1.0f / 60.0f;
	constexpr int32 SoloWaterResolution = 192;
	constexpr double SoloWaterExtentCm = 3600.0;
	constexpr int32 SoloOracleResolution = 192;
	constexpr float SoloFloorTopZCm = -100.0f;
	constexpr float SoloCubeHalfSizeCm = 50.0f;
	constexpr int32 SoloTerrainWaitTickLimit = 900;
	constexpr int32 MaximumReadbackLatencyFrames = 3;
	constexpr double SoloGuideWeightThreshold = 0.5;
	constexpr double SeedSpeedRatioMinimum = 0.25;
	constexpr float SoloEncodingTolerance = 1.0e-3f;
	const TCHAR* const SoloWaterGpuVariableName = TEXT("TD.FluidFX.WaterGpu");
	const TCHAR* const SoloSolverBackendVariableName = TEXT("TD.FluidFX.SolverBackend");
	const TCHAR* const SoloVelocityFormatVariableName = TEXT("TD.FluidFX.WaterGpuVelocityFloat32");
	const TCHAR* const SoloCubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const SoloShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	const FVector SoloSmokeAnchorLocation(20000.0, 0.0, 0.0);
	constexpr double SoloSmokeSpacingCm = 8000.0;

	FString MakeSoloMissingWaterGpuMessage(const FString& Label)
	{
		return FString::Printf(TEXT("%s: TD.FluidFX.WaterGpu is not registered, so the water layer still forces the CPU solver (FX-4b not implemented)"), *Label);
	}

	FString MakeSoloGpuNotSelectedMessage(const FString& Label)
	{
		return FString::Printf(TEXT("%s: the GPU water actor did not select the GPU solver (water layer forces CPU before FX-4b)"), *Label);
	}

	FString MakeSoloNoWaterActivityMessage(const FString& Label, const FString& GateDetail)
	{
		return FString::Printf(TEXT("%s: GPU water outputs show no water activity (A0 %s)"), *Label, *GateDetail);
	}

	FString MakeSoloTerrainTimeoutMessage(const FString& Label, int32 TickLimit)
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

	bool CanRunSoloWaterTest(FAutomationTestBase& Test, const FString& CaseLabel)
	{
		const bool bHasActiveRenderingRhi = !GUsingNullRHI && FApp::CanEverRender();
		UE_LOG(LogTDFluidGpuWaterSoloRhiTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), *CaseLabel, bHasActiveRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (bHasActiveRenderingRhi)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), *CaseLabel));
		return false;
	}

	void ReportSoloLine(FAutomationTestBase& Test, const FString& Line)
	{
		UE_LOG(LogTDFluidGpuWaterSoloRhiTests, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
	}

	struct FTDFluidGpuWaterSoloTestWorld
	{
		FTDFluidGpuWaterSoloTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidGpuWaterSoloWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidGpuWaterSoloTestWorld()
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

		FTDFluidGpuWaterSoloTestWorld(const FTDFluidGpuWaterSoloTestWorld&) = delete;
		FTDFluidGpuWaterSoloTestWorld& operator=(const FTDFluidGpuWaterSoloTestWorld&) = delete;

		UTDFluidLiveSubsystem* GetFluidSubsystem() const
		{
			return World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
		}

		void StepWorld(float DeltaSeconds)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
			ENQUEUE_RENDER_COMMAND(TDFluidGpuWaterSoloWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	FTDFluidSimulationConfig MakeSoloWaterConfig()
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.SolverBackend = ETDFluidSolverBackend::Gpu;
		Config.GridResolution = SoloWaterResolution;
		Config.SimExtentCm = FVector2D(SoloWaterExtentCm, SoloWaterExtentCm);
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

	FTDFluidSimulationConfig MakeSoloSmokeConfig(int32 Resolution)
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
		Config.MaxPointsPerTick = 64;
		Config.bEnableWaterLayer = false;
		return Config;
	}

	FTDFluidBrushStamp MakeSoloSmokeStamp(const FVector& CenterLocation, int32 StepIndex)
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

	ATDFluidLiveActor* SpawnSoloLiveActor(UWorld& SoloWorld, const FVector& Location, const FTDFluidSimulationConfig& Config)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
		ATDFluidLiveActor* LiveActor = SoloWorld.SpawnActorDeferred<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), SpawnTransform);
		if (!LiveActor || !LiveActor->LiveComponent)
		{
			return nullptr;
		}

		LiveActor->LiveComponent->SimulationConfig = Config;
		LiveActor->FinishSpawning(SpawnTransform);
		return LiveActor;
	}

	struct FTDFluidSoloWaterScene
	{
		AActor* Anchor = nullptr;
		UStaticMeshComponent* SurfaceMesh = nullptr;
		ATDFluidWaterActor* WaterActor = nullptr;
		UTDFluidPresetAsset* Preset = nullptr;
		TArray<FTDFluidFlowGuide> Guides;

		UMaterialInstanceDynamic* GetSurfaceMaterial() const
		{
			return SurfaceMesh ? Cast<UMaterialInstanceDynamic>(SurfaceMesh->GetMaterial(0)) : nullptr;
		}
	};

	bool SpawnSoloWaterScene(UWorld& SoloWorld, FTDFluidSoloWaterScene& OutScene)
	{
		UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, SoloCubeMeshPath);
		UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, SoloShapeMaterialPath);
		if (!CubeMesh || !ShapeMaterial)
		{
			return false;
		}

		AActor* FloorActor = SoloWorld.SpawnActor<AActor>();
		if (!FloorActor)
		{
			return false;
		}
		UStaticMeshComponent* FloorMesh = NewObject<UStaticMeshComponent>(FloorActor);
		FloorActor->AddInstanceComponent(FloorMesh);
		FloorActor->SetRootComponent(FloorMesh);
		FloorMesh->SetMobility(EComponentMobility::Movable);
		FloorMesh->SetStaticMesh(CubeMesh);
		FloorMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		FloorMesh->SetCollisionObjectType(ECC_WorldStatic);
		FloorMesh->SetCollisionResponseToAllChannels(ECR_Block);
		FloorMesh->SetWorldTransform(FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, SoloFloorTopZCm - SoloCubeHalfSizeCm), FVector(100.0, 100.0, 1.0)));
		FloorMesh->RegisterComponent();

		ATDFluidRiverActor* River = SoloWorld.SpawnActorDeferred<ATDFluidRiverActor>(ATDFluidRiverActor::StaticClass(), FTransform::Identity);
		if (!River || !River->RiverSpline)
		{
			return false;
		}
		River->RiverWidthCm = 800.0f;
		River->FlowSpeedCmPerSec = 200.0f;
		River->GuideSampleSpacingCm = 50.0f;
		River->bBuildRibbonSurface = false;
		USplineComponent* Spline = River->RiverSpline;
		Spline->ClearSplinePoints(false);
		Spline->AddSplinePoint(FVector(-2700.0, 0.0, 0.0), ESplineCoordinateSpace::World, false);
		Spline->AddSplinePoint(FVector(2700.0, 0.0, 0.0), ESplineCoordinateSpace::World, false);
		Spline->SetSplinePointType(0, ESplinePointType::Linear, false);
		Spline->SetSplinePointType(1, ESplinePointType::Linear, false);
		Spline->UpdateSpline();
		River->FinishSpawning(FTransform::Identity);
		River->BuildFlowGuide(OutScene.Guides.AddDefaulted_GetRef());

		AActor* SurfaceActor = SoloWorld.SpawnActor<AActor>();
		if (!SurfaceActor)
		{
			return false;
		}
		OutScene.SurfaceMesh = NewObject<UStaticMeshComponent>(SurfaceActor);
		SurfaceActor->AddInstanceComponent(OutScene.SurfaceMesh);
		SurfaceActor->SetRootComponent(OutScene.SurfaceMesh);
		OutScene.SurfaceMesh->SetMobility(EComponentMobility::Movable);
		OutScene.SurfaceMesh->SetStaticMesh(CubeMesh);
		OutScene.SurfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OutScene.SurfaceMesh->SetMaterial(0, ShapeMaterial);
		OutScene.SurfaceMesh->RegisterComponent();
		SurfaceActor->SetActorLocation(FVector(0.0, 0.0, 400.0));
		SurfaceActor->Tags.Add(FName(TEXT("TDFluidWaterSurface")));

		OutScene.Anchor = SoloWorld.SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (!OutScene.Anchor)
		{
			return false;
		}
		USceneComponent* AnchorRoot = NewObject<USceneComponent>(OutScene.Anchor);
		OutScene.Anchor->AddInstanceComponent(AnchorRoot);
		OutScene.Anchor->SetRootComponent(AnchorRoot);
		AnchorRoot->RegisterComponent();

		OutScene.WaterActor = SoloWorld.SpawnActor<ATDFluidWaterActor>(ATDFluidWaterActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (!OutScene.WaterActor || !OutScene.WaterActor->LiveComponent)
		{
			return false;
		}
		OutScene.WaterActor->FollowTargetActor = OutScene.Anchor;
		OutScene.WaterActor->ViewLookAheadCm = 0.0f;
		OutScene.WaterActor->bSleepWhenNoWaterInWindow = false;
		OutScene.Preset = UTDFluidPresetAsset::CreateTransientPreset(&SoloWorld, ETDFluidPresetKind::River);
		OutScene.Preset->bOverridePresetDefaults = true;
		OutScene.Preset->SimulationConfig = MakeSoloWaterConfig();
		OutScene.WaterActor->InitializeFromPreset(OutScene.Preset, 0.0f);
		return true;
	}

	UTexture* ResolveSoloSurfaceTexture(UMaterialInstanceDynamic* SurfaceMaterial)
	{
		return SurfaceMaterial ? SurfaceMaterial->K2_GetTextureParameterValue(TEXT("WaterSurfaceTexture")) : nullptr;
	}

	bool ReadSoloRenderTargetPixels(UTexture* OutputTexture, TArray<FFloat16Color>& OutPixels)
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

	bool IsSurfaceTargetAtWaterTier(UMaterialInstanceDynamic* SurfaceMaterial)
	{
		const UTextureRenderTarget2D* SurfaceTarget = Cast<UTextureRenderTarget2D>(ResolveSoloSurfaceTexture(SurfaceMaterial));
		return SurfaceTarget && SurfaceTarget->SizeX == SoloWaterResolution && SurfaceTarget->SizeY == SoloWaterResolution && SurfaceTarget->GetFormat() == PF_FloatRGBA;
	}

	struct FTDFluidSoloGuideOracle
	{
		int32 Resolution = 0;
		FVector2D MinimumCorner = FVector2D::ZeroVector;
		FVector2D ExtentCm = FVector2D::ZeroVector;
		TArray<float> Weights;
		TArray<FVector2D> Targets;

		int32 FindCellIndex(const FVector2D& WorldPoint) const
		{
			if (Resolution <= 0)
			{
				return INDEX_NONE;
			}

			const int32 CellX = FMath::FloorToInt32((WorldPoint.X - MinimumCorner.X) / ExtentCm.X * Resolution);
			const int32 CellY = FMath::FloorToInt32((WorldPoint.Y - MinimumCorner.Y) / ExtentCm.Y * Resolution);
			if (CellX < 0 || CellX >= Resolution || CellY < 0 || CellY >= Resolution)
			{
				return INDEX_NONE;
			}
			return CellY * Resolution + CellX;
		}
	};

	FTDFluidSoloGuideOracle BuildSoloGuideOracle(const UTDFluidLiveComponent& Component, const TArray<FTDFluidFlowGuide>& Guides)
	{
		FTDFluidSimulationConfig OracleConfig = MakeSoloWaterConfig();
		OracleConfig.SolverBackend = ETDFluidSolverBackend::Cpu;
		OracleConfig.GridResolution = SoloOracleResolution;
		OracleConfig.SimExtentCm = Component.GetSimulationExtentCm();
		FTDFluidGrid2DSolver OracleSolver;
		OracleSolver.Initialize(OracleConfig, Component.GetSimulationCenterWorld());
		OracleSolver.SetFlowGuides(Guides);
		OracleSolver.StepSimulation(SoloStepSeconds);

		FTDFluidSoloGuideOracle Oracle;
		Oracle.Resolution = OracleSolver.GetResolution();
		Oracle.ExtentCm = OracleSolver.GetExtentCm();
		Oracle.MinimumCorner = OracleSolver.GetCenterWorld() - 0.5 * Oracle.ExtentCm;
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

	struct FTDFluidSoloFlowGate
	{
		int32 GuideCellCount = 0;
		double DepthFraction = 0.0;
		double SpeedRatio = 0.0;
		double ChangedFraction = 0.0;

		bool IsActive() const
		{
			return GuideCellCount > 0 && DepthFraction >= 0.9 && SpeedRatio >= 0.5 && ChangedFraction >= 0.5;
		}

		FString Describe() const
		{
			return FString::Printf(TEXT("flow guide_cells=%d depth_fraction=%.3f speed_ratio=%.3f changed_fraction=%.3f"), GuideCellCount, DepthFraction, SpeedRatio, ChangedFraction);
		}
	};

	FTDFluidSoloFlowGate MeasureSoloFlowGate(const UTDFluidLiveComponent& Component, UMaterialInstanceDynamic* SurfaceMaterial, const FTDFluidSoloGuideOracle& Oracle)
	{
		FTDFluidSoloFlowGate Gate;
		TArray<FFloat16Color> VelocityPixels;
		TArray<FFloat16Color> SurfacePixels;
		const int32 Resolution = Component.GetSimulationResolution();
		if (!ReadSoloRenderTargetPixels(Component.GetVelocityTexture(), VelocityPixels) || !ReadSoloRenderTargetPixels(ResolveSoloSurfaceTexture(SurfaceMaterial), SurfacePixels))
		{
			return Gate;
		}
		if (Resolution <= 0 || VelocityPixels.Num() != Resolution * Resolution || SurfacePixels.Num() != VelocityPixels.Num())
		{
			return Gate;
		}

		const double SpeedReference = static_cast<double>(FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec());
		const FVector2D ExtentCm = Component.GetSimulationExtentCm();
		const FVector2D MinimumCorner = Component.GetSimulationCenterWorld() - 0.5 * ExtentCm;
		const FVector2D CellSize = ExtentCm / static_cast<double>(Resolution);
		int32 DeepCellCount = 0;
		int32 ChangedCellCount = 0;
		double SpeedSum = 0.0;
		double TargetSpeedSum = 0.0;
		for (int32 CellY = 0; CellY < Resolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < Resolution; ++CellX)
			{
				const FVector2D CellWorld = MinimumCorner + FVector2D((CellX + 0.5) * CellSize.X, (CellY + 0.5) * CellSize.Y);
				const int32 OracleIndex = Oracle.FindCellIndex(CellWorld);
				if (OracleIndex == INDEX_NONE || Oracle.Weights[OracleIndex] < SoloGuideWeightThreshold)
				{
					continue;
				}

				const FFloat16Color& VelocityPixel = VelocityPixels[CellY * Resolution + CellX];
				const bool bMatchesClearColor = FMath::IsNearlyEqual(VelocityPixel.R.GetFloat(), 0.5f, 1.0e-4f)
					&& FMath::IsNearlyEqual(VelocityPixel.G.GetFloat(), 0.5f, 1.0e-4f)
					&& FMath::IsNearlyEqual(VelocityPixel.B.GetFloat(), 0.0f, 1.0e-4f)
					&& FMath::IsNearlyEqual(VelocityPixel.A.GetFloat(), 0.0f, 1.0e-4f);
				const FVector2D DecodedVelocity((VelocityPixel.R.GetFloat() * 2.0 - 1.0) * SpeedReference, (VelocityPixel.G.GetFloat() * 2.0 - 1.0) * SpeedReference);
				++Gate.GuideCellCount;
				DeepCellCount += SurfacePixels[CellY * Resolution + CellX].R.GetFloat() > 0.0f ? 1 : 0;
				ChangedCellCount += bMatchesClearColor ? 0 : 1;
				SpeedSum += DecodedVelocity.Size();
				TargetSpeedSum += Oracle.Targets[OracleIndex].Size();
			}
		}

		Gate.DepthFraction = Gate.GuideCellCount > 0 ? static_cast<double>(DeepCellCount) / Gate.GuideCellCount : 0.0;
		Gate.ChangedFraction = Gate.GuideCellCount > 0 ? static_cast<double>(ChangedCellCount) / Gate.GuideCellCount : 0.0;
		Gate.SpeedRatio = TargetSpeedSum > 0.0 ? SpeedSum / TargetSpeedSum : 0.0;
		return Gate;
	}

	bool WaitForSoloTerrain(FAutomationTestBase& Test, const FString& Label, FTDFluidGpuWaterSoloTestWorld& SoloWorld, const UTDFluidLiveComponent& Component)
	{
		for (int32 TickIndex = 0; TickIndex < SoloTerrainWaitTickLimit; ++TickIndex)
		{
			if (Component.GetWaterLayerDisplayAlpha() > 0.0f)
			{
				return true;
			}
			SoloWorld.StepWorld(SoloStepSeconds);
		}

		if (Component.GetWaterLayerDisplayAlpha() > 0.0f)
		{
			return true;
		}

		Test.AddError(MakeSoloTerrainTimeoutMessage(Label, SoloTerrainWaitTickLimit));
		return false;
	}

	bool HasFreshReadback(const UTDFluidLiveComponent& Component, uint32 FrameBeforeStage)
	{
		const FTDFluidReadbackField& Field = Component.GetGpuReadbackField();
		if (!Component.IsUsingGpuSolver() || !Field.IsValid() || Field.FrameNumber <= FrameBeforeStage)
		{
			return false;
		}

		const FVector2D CenterWorld = Component.GetSimulationCenterWorld();
		FTDFluidCellSample Sample;
		return Component.SampleFluidAtWorldLocation(FVector(CenterWorld.X, CenterWorld.Y, 0.0), Sample);
	}

	bool ExpectReadbacksWithinLatency(FAutomationTestBase& Test, const FString& StageLabel, FTDFluidGpuWaterSoloTestWorld& SoloWorld, const UTDFluidLiveSubsystem& FluidSubsystem, const TArray<ATDFluidLiveActor*>& LiveActors, TFunctionRef<void(int32)> QueueStepStamps)
	{
		const uint32 FrameBeforeStage = FluidSubsystem.GetLastGpuSolverFrameNumber();
		TArray<bool> FreshFlags;
		FreshFlags.Init(false, LiveActors.Num());
		for (int32 FrameIndex = 0; FrameIndex < MaximumReadbackLatencyFrames; ++FrameIndex)
		{
			QueueStepStamps(FrameIndex);
			SoloWorld.StepWorld(SoloStepSeconds);
			for (int32 ActorIndex = 0; ActorIndex < LiveActors.Num(); ++ActorIndex)
			{
				FreshFlags[ActorIndex] = FreshFlags[ActorIndex] || HasFreshReadback(*LiveActors[ActorIndex]->LiveComponent, FrameBeforeStage);
			}
		}

		bool bAllFresh = true;
		for (int32 ActorIndex = 0; ActorIndex < LiveActors.Num(); ++ActorIndex)
		{
			const FTDFluidReadbackField& Field = LiveActors[ActorIndex]->LiveComponent->GetGpuReadbackField();
			Test.TestTrue(*FString::Printf(TEXT("%s: actor %d returned a valid sample from a readback newer than frame %u within three frames (latest %u)"), *StageLabel, ActorIndex, FrameBeforeStage, Field.FrameNumber), FreshFlags[ActorIndex]);
			bAllFresh = bAllFresh && FreshFlags[ActorIndex];
		}
		return bAllFresh;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterSoloReadbackRingSurvivesWaterJoinTest,
	"TDGame.FluidFXRhi.SoloWater.ReadbackRingSurvivesWaterJoin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterSoloReadbackRingSurvivesWaterJoinTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterSoloRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.SoloWater.ReadbackRingSurvivesWaterJoin");
	if (!CanRunSoloWaterTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SoloSolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(SoloWaterGpuVariableName, 1);
	if (!WaterGpuScope.IsValid())
	{
		AddError(MakeSoloMissingWaterGpuMessage(CaseLabel));
		return false;
	}

	FTDFluidGpuWaterSoloTestWorld SoloWorld;
	const UTDFluidLiveSubsystem* FluidSubsystem = SoloWorld.GetFluidSubsystem();
	if (!TestNotNull(*(CaseLabel + TEXT(" has a fluid subsystem")), FluidSubsystem))
	{
		return false;
	}

	TArray<ATDFluidLiveActor*> LiveActors;
	LiveActors.Add(SpawnSoloLiveActor(*SoloWorld.World, SoloSmokeAnchorLocation, MakeSoloSmokeConfig(128)));
	if (!TestNotNull(*(CaseLabel + TEXT(" spawned the first GPU smoke actor")), LiveActors[0]))
	{
		return false;
	}

	int32 StampStep = 0;
	const auto QueueSmokeStamps = [&LiveActors, &StampStep](int32)
	{
		for (ATDFluidLiveActor* LiveActor : LiveActors)
		{
			if (LiveActor && !LiveActor->IsA<ATDFluidWaterActor>())
			{
				LiveActor->LiveComponent->QueueBrushStamp(MakeSoloSmokeStamp(LiveActor->GetActorLocation(), StampStep));
			}
		}
		++StampStep;
	};

	ExpectReadbacksWithinLatency(*this, CaseLabel + TEXT(" one GPU actor"), SoloWorld, *FluidSubsystem, LiveActors, QueueSmokeStamps);

	FTDFluidSoloWaterScene WaterScene;
	if (!TestTrue(*(CaseLabel + TEXT(" spawned the water scene")), SpawnSoloWaterScene(*SoloWorld.World, WaterScene)))
	{
		return false;
	}
	LiveActors.Add(WaterScene.WaterActor);
	LiveActors.Add(SpawnSoloLiveActor(*SoloWorld.World, SoloSmokeAnchorLocation + FVector(SoloSmokeSpacingCm, 0.0, 0.0), MakeSoloSmokeConfig(128)));
	LiveActors.Add(SpawnSoloLiveActor(*SoloWorld.World, SoloSmokeAnchorLocation + FVector(2.0 * SoloSmokeSpacingCm, 0.0, 0.0), MakeSoloSmokeConfig(256)));
	if (!TestTrue(*(CaseLabel + TEXT(" spawned two more GPU smoke actors")), LiveActors[2] != nullptr && LiveActors[3] != nullptr))
	{
		return false;
	}

	SoloWorld.StepWorld(SoloStepSeconds);
	if (!WaterScene.WaterActor->LiveComponent->IsUsingGpuSolver())
	{
		AddError(MakeSoloGpuNotSelectedMessage(CaseLabel));
		return false;
	}

	ExpectReadbacksWithinLatency(*this, CaseLabel + TEXT(" four GPU actors including water"), SoloWorld, *FluidSubsystem, LiveActors, QueueSmokeStamps);

	for (int32 ActorIndex = LiveActors.Num() - 1; ActorIndex >= 1; --ActorIndex)
	{
		SoloWorld.World->DestroyActor(LiveActors[ActorIndex]);
		LiveActors.RemoveAt(ActorIndex);
	}

	ExpectReadbacksWithinLatency(*this, CaseLabel + TEXT(" back to one GPU actor"), SoloWorld, *FluidSubsystem, LiveActors, QueueSmokeStamps);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterSoloLiveToggleKeepsReadbackValidTest,
	"TDGame.FluidFXRhi.SoloWater.LiveToggleKeepsReadbackValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterSoloLiveToggleKeepsReadbackValidTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterSoloRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.SoloWater.LiveToggleKeepsReadbackValid");
	if (!CanRunSoloWaterTest(*this, CaseLabel))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope SolverBackendScope(SoloSolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(SoloWaterGpuVariableName, 1);
	if (!WaterGpuScope.IsValid())
	{
		AddError(MakeSoloMissingWaterGpuMessage(CaseLabel));
		return false;
	}

	FTDFluidGpuWaterSoloTestWorld SoloWorld;
	const UTDFluidLiveSubsystem* FluidSubsystem = SoloWorld.GetFluidSubsystem();
	FTDFluidSoloWaterScene WaterScene;
	if (!TestNotNull(*(CaseLabel + TEXT(" has a fluid subsystem")), FluidSubsystem) || !TestTrue(*(CaseLabel + TEXT(" spawned the water scene")), SpawnSoloWaterScene(*SoloWorld.World, WaterScene)))
	{
		return false;
	}

	ATDFluidWaterActor& WaterActor = *WaterScene.WaterActor;
	UTDFluidLiveComponent& WaterComponent = *WaterActor.LiveComponent;
	SoloWorld.StepWorld(SoloStepSeconds);
	if (!WaterComponent.IsUsingGpuSolver())
	{
		AddError(MakeSoloGpuNotSelectedMessage(CaseLabel));
		return false;
	}
	if (!WaitForSoloTerrain(*this, CaseLabel, SoloWorld, WaterComponent))
	{
		return false;
	}

	const TArray<ATDFluidLiveActor*> WaterActors = { &WaterActor };
	const auto NoStamps = [](int32) {};
	if (!ExpectReadbacksWithinLatency(*this, CaseLabel + TEXT(" after terrain completion"), SoloWorld, *FluidSubsystem, WaterActors, NoStamps))
	{
		return false;
	}

	const FTDFluidSoloGuideOracle Oracle = BuildSoloGuideOracle(WaterComponent, WaterScene.Guides);
	const FTDFluidSoloFlowGate InitialGate = MeasureSoloFlowGate(WaterComponent, WaterScene.GetSurfaceMaterial(), Oracle);
	ReportSoloLine(*this, FString::Printf(TEXT("TDFluidGpuWaterToggle: %s initial %s"), *CaseLabel, *InitialGate.Describe()));
	if (!InitialGate.IsActive())
	{
		AddError(MakeSoloNoWaterActivityMessage(CaseLabel + TEXT(" initial"), InitialGate.Describe()));
		return false;
	}

	FTDWaterGpuConsoleVariableScope VelocityFormatScope(SoloVelocityFormatVariableName, -1);
	if (!VelocityFormatScope.IsValid())
	{
		AddError(FString::Printf(TEXT("%s: %s is not registered, so the 192 key change path cannot be exercised"), *CaseLabel, SoloVelocityFormatVariableName));
		return false;
	}

	for (const int32 FormatValue : { 0, -1 })
	{
		const FString FormatLabel = FString::Printf(TEXT("%s velocity format %d"), *CaseLabel, FormatValue);
		VelocityFormatScope.SetValue(FormatValue);
		SoloWorld.StepWorld(SoloStepSeconds);
		const FTDFluidSoloFlowGate SeedGate = MeasureSoloFlowGate(WaterComponent, WaterScene.GetSurfaceMaterial(), Oracle);
		ReportSoloLine(*this, FString::Printf(TEXT("TDFluidGpuWaterToggle: %s first frame %s"), *FormatLabel, *SeedGate.Describe()));
		TestTrue(*FString::Printf(TEXT("%s first frame guide cell speed ratio %.3f is at least 0.25 of the target (the seed read the guides)"), *FormatLabel, SeedGate.SpeedRatio), SeedGate.GuideCellCount > 0 && SeedGate.SpeedRatio >= SeedSpeedRatioMinimum);

		FTDFluidSoloFlowGate RecoveryGate = SeedGate;
		for (int32 FrameIndex = 1; FrameIndex < MaximumReadbackLatencyFrames && !RecoveryGate.IsActive(); ++FrameIndex)
		{
			SoloWorld.StepWorld(SoloStepSeconds);
			RecoveryGate = MeasureSoloFlowGate(WaterComponent, WaterScene.GetSurfaceMaterial(), Oracle);
		}
		if (!RecoveryGate.IsActive())
		{
			AddError(MakeSoloNoWaterActivityMessage(FormatLabel, RecoveryGate.Describe()));
			return false;
		}
		ExpectReadbacksWithinLatency(*this, FormatLabel, SoloWorld, *FluidSubsystem, WaterActors, NoStamps);
	}

	FTDFluidSimulationConfig DryConfig = MakeSoloWaterConfig();
	DryConfig.bEnableWaterLayer = false;
	WaterComponent.ApplySimulationConfig(DryConfig);
	ExpectReadbacksWithinLatency(*this, CaseLabel + TEXT(" water layer turned off"), SoloWorld, *FluidSubsystem, WaterActors, NoStamps);

	WaterComponent.ApplySimulationConfig(MakeSoloWaterConfig());
	ExpectReadbacksWithinLatency(*this, CaseLabel + TEXT(" water layer turned on again"), SoloWorld, *FluidSubsystem, WaterActors, NoStamps);
	TestTrue(*(CaseLabel + TEXT(" water surface render target is valid again after the water layer returns")), IsSurfaceTargetAtWaterTier(WaterScene.GetSurfaceMaterial()));

	WaterComponent.SetSimulationAwake(false);
	SoloWorld.StepWorld(SoloStepSeconds);
	SoloWorld.StepWorld(SoloStepSeconds);
	WaterComponent.SetSimulationAwake(true);
	ExpectReadbacksWithinLatency(*this, CaseLabel + TEXT(" after sleep and wake"), SoloWorld, *FluidSubsystem, WaterActors, NoStamps);

	UTextureRenderTarget2D* SurfaceTarget = Cast<UTextureRenderTarget2D>(ResolveSoloSurfaceTexture(WaterScene.GetSurfaceMaterial()));
	if (!TestNotNull(*(CaseLabel + TEXT(" has a water surface render target before the pool return")), SurfaceTarget))
	{
		return false;
	}

	WaterActor.SetPooledInactive(true);
	FlushRenderingCommands();
	TArray<FFloat16Color> ClearedPixels;
	const bool bReadClearedPixels = ReadSoloRenderTargetPixels(SurfaceTarget, ClearedPixels);
	int32 UnclearedPixelCount = 0;
	for (const FFloat16Color& Pixel : ClearedPixels)
	{
		const bool bIsCleared = Pixel.R.GetFloat() == 0.0f && Pixel.G.GetFloat() == 0.0f && FMath::IsNearlyEqual(Pixel.B.GetFloat(), 0.5f, SoloEncodingTolerance);
		UnclearedPixelCount += bIsCleared ? 0 : 1;
	}
	ReportSoloLine(*this, FString::Printf(TEXT("TDFluidGpuWaterToggle: %s pool return read=%d pixels=%d uncleared=%d"), *CaseLabel, bReadClearedPixels ? 1 : 0, ClearedPixels.Num(), UnclearedPixelCount));
	TestTrue(*(CaseLabel + TEXT(" read the water surface render target after the pool return")), bReadClearedPixels);
	TestEqual(*(CaseLabel + TEXT(" water surface pixels that keep depth, wetness or ripples after the pool return")), UnclearedPixelCount, 0);

	WaterActor.SetPooledInactive(false);
	ExpectReadbacksWithinLatency(*this, CaseLabel + TEXT(" after leaving the pool"), SoloWorld, *FluidSubsystem, WaterActors, NoStamps);
	TestTrue(*(CaseLabel + TEXT(" water surface render target is valid again after leaving the pool")), IsSurfaceTargetAtWaterTier(WaterScene.GetSurfaceMaterial()));
	return !HasAnyErrors();
}

#endif
