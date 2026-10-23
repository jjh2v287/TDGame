#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidGpuWaterFieldState.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidRiverActor.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterActor.h"
#include "FluidFX/TDFluidWaterInputRaster.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "TDFluidGpuBackendSelection.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuInputs.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidGpuWaterContractTests, Log, All);

namespace TDFluidGpuWaterContractTestPrivate
{
	constexpr float ContractStepSeconds = 1.0f / 60.0f;
	constexpr float SquareCellSizeCm = 18.75f;
	constexpr float GuideSampleSpacingCm = 50.0f;
	constexpr float FlatBedElevationCm = -100.0f;
	constexpr int32 TerrainCompletionTickLimit = 900;
	constexpr int32 MaximumProbeFrameAge = 3;
	constexpr int32 ProbeGenerationFrameLimit = 3;
	constexpr int32 ProbeComparisonFrameCount = 20;
	constexpr int32 StoredOutputFrameCount = 8;
	constexpr int32 ExpectedFullUploadGatherCount = 3;
	constexpr int32 RiverSceneResolution = 192;
	constexpr float RiverSceneExtentCm = 3600.0f;
	constexpr float RiverSurfaceZCm = 50.0f;
	constexpr float RiverWidthCm = 2400.0f;
	constexpr float RiverFlowSpeedCmPerSec = 250.0f;
	constexpr double RiverHalfLengthCm = 3000.0;
	constexpr float TerrainSlabHalfHeightCm = 50.0f;
	constexpr float TerrainSlabHalfSizeCm = 5000.0f;
	constexpr float RockHalfSizeCm = 80.0f;
	constexpr int32 RiverScenePressureIterations = 12;
	constexpr double DryBandMinimumOffsetCm = 1450.0;
	constexpr double DryBandMaximumOffsetCm = 1650.0;
	constexpr double DryBandHalfLengthCm = 1400.0;
	constexpr float ProbeVelocityToleranceCmPerSec = 0.6f;
	constexpr float ProbeDepthToleranceCm = 0.15f;
	constexpr float ProbeWhitewaterTolerance = 0.0005f;
	constexpr float ProbeRippleToleranceCm = 0.01f;
	constexpr float SaturationMargin = 0.001f;
	constexpr float FlowWetFractionThreshold = 0.9f;
	constexpr float FlowSpeedRatioThreshold = 0.5f;
	constexpr float FlowChangedFractionThreshold = 0.5f;
	constexpr float SeededSpeedRatioThreshold = 0.25f;
	constexpr float StrongGuideWeight = 0.5f;
	constexpr int32 UpdateSettingsSettleFrames = 10;
	constexpr int32 FlowRecoveryFrameLimit = 60;
	const FVector2D RockCenterXY(600.0, 0.0);
	const TCHAR* const WaterGpuVariableName = TEXT("TD.FluidFX.WaterGpu");
	const TCHAR* const SolverBackendVariableName = TEXT("TD.FluidFX.SolverBackend");
	const TCHAR* const VelocityFloat32VariableName = TEXT("TD.FluidFX.WaterGpuVelocityFloat32");

	FString MakeWaterGpuMissingMessage(const FString& Label)
	{
		return FString::Printf(TEXT("%s: TD.FluidFX.WaterGpu is not registered, so the water layer still forces the CPU solver (FX-4b not implemented)"), *Label);
	}

	FString MakeGpuWaterNotSelectedMessage(const FString& Label)
	{
		return FString::Printf(TEXT("%s: the GPU water actor did not select the GPU solver (water layer forces CPU before FX-4b)"), *Label);
	}

	FString MakeNoWaterActivityMessage(const FString& Label, const TCHAR* ActivityKind)
	{
		return FString::Printf(TEXT("%s: GPU water outputs show no water activity (A0 %s)"), *Label, ActivityKind);
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

			PreviousValue = Variable->GetInt();
			Variable->Set(Value, ECVF_SetByCode);
		}

		~FTDWaterGpuConsoleVariableScope()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		bool IsValid() const
		{
			return Variable != nullptr;
		}

		void SetValue(int32 Value)
		{
			if (Variable)
			{
				Variable->Set(Value, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 0;
	};

	struct FTDWaterContractDenormalScope
	{
		FTDWaterContractDenormalScope()
			: CallerControlRegister(VectorGetControlRegister())
		{
			VectorSetControlRegister(CallerControlRegister & ~static_cast<uint32>(VECTOR_DENORMALS_FLUSH_TO_ZERO));
		}

		~FTDWaterContractDenormalScope()
		{
			VectorSetControlRegister(CallerControlRegister);
		}

		uint32 CallerControlRegister;
	};

	struct FTDFluidGpuWaterContractTestWorld
	{
		FTDFluidGpuWaterContractTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidGpuWaterContractWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidGpuWaterContractTestWorld()
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

		UTDFluidLiveSubsystem* GetFluidSubsystem() const
		{
			return World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
		}

		void StepWorld(float DeltaSeconds, bool bShouldWaitForGpu)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
			if (bShouldWaitForGpu)
			{
				ENQUEUE_RENDER_COMMAND(TDFluidGpuWaterContractWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
				{
					RHICmdList.SubmitAndBlockUntilGPUIdle();
				});
			}
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	struct FTDWaterOutputFrame
	{
		TArray<FFloat16Color> DensityPressure;
		TArray<FFloat16Color> Velocity;
		TArray<FFloat16Color> WaterSurface;
	};

	struct FTDWaterGuideCell
	{
		int32 CellIndex = 0;
		FVector2D TargetVelocityCmPerSec = FVector2D::ZeroVector;
		float GuideWeight = 0.0f;
	};

	struct FTDWaterFlowActivity
	{
		double WetFraction = 0.0;
		double MeanAlongTargetCmPerSec = 0.0;
		double MeanTargetCmPerSec = 0.0;
		double ChangedVelocityFraction = 0.0;
	};

	struct FTDWaterRiverScene
	{
		ATDFluidWaterActor* WaterActor = nullptr;
		AActor* Anchor = nullptr;
		ATDFluidRiverActor* River = nullptr;
	};

	struct FTDProbeComparisonTally
	{
		int32 ComparedSampleCount = 0;
		int32 MismatchCount = 0;
		int32 MissingFrameCount = 0;
		int32 FrameAgeViolationCount = 0;
		int32 DroppedRingFrameCount = 0;
		int32 InsideFlagMismatchCount = 0;
		int32 LocationMismatchCount = 0;
		FString FirstMismatch;
	};

	bool AreFloatBitsEqual(float Left, float Right)
	{
		return FMemory::Memcmp(&Left, &Right, sizeof(float)) == 0;
	}

	bool AreDoubleBitsEqual(double Left, double Right)
	{
		return FMemory::Memcmp(&Left, &Right, sizeof(double)) == 0;
	}

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	bool CanRunRhiContractTest(FAutomationTestBase& Test, const FString& Label)
	{
		const bool bHasRenderingRhi = HasRenderingRhi();
		UE_LOG(LogTDFluidGpuWaterContractTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), *Label, bHasRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (bHasRenderingRhi)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), *Label));
		return false;
	}

	bool RequireWaterGpuVariable(FAutomationTestBase& Test, const FString& Label)
	{
		if (IConsoleManager::Get().FindConsoleVariable(WaterGpuVariableName))
		{
			return true;
		}

		Test.AddError(MakeWaterGpuMissingMessage(Label));
		return false;
	}

	bool RequireGpuWaterLayer(FAutomationTestBase& Test, const FString& Label, const UTDFluidLiveComponent& Component)
	{
		if (Component.IsUsingGpuSolver() && Component.IsUsingGpuWaterLayer())
		{
			return true;
		}

		Test.AddError(MakeGpuWaterNotSelectedMessage(Label));
		return false;
	}

	void ReportLine(FAutomationTestBase& Test, const FString& Line)
	{
		UE_LOG(LogTDFluidGpuWaterContractTests, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
	}

	FTDFluidSimulationConfig MakeContractWaterConfig(int32 Resolution)
	{
		FTDFluidSimulationConfig Config;
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = Resolution;
		Config.SimExtentCm = FVector2D(Resolution * SquareCellSizeCm, Resolution * SquareCellSizeCm);
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

	FTDFluidFlowGuidePoint MakeGuidePoint(const FVector2D& Position, float SurfaceZCm, float HalfWidthCm, float SpeedCmPerSec)
	{
		FTDFluidFlowGuidePoint Point;
		Point.WorldLocation = FVector(Position.X, Position.Y, SurfaceZCm);
		Point.HalfWidthCm = HalfWidthCm;
		Point.FlowSpeedCmPerSec = SpeedCmPerSec;
		return Point;
	}

	FTDFluidFlowGuide MakeStraightGuide(const FVector2D& Start, const FVector2D& End, float HalfWidthCm, float SpeedCmPerSec, float SurfaceZCm)
	{
		FTDFluidFlowGuide Guide;
		const double Length = (End - Start).Size();
		const int32 SegmentCount = FMath::Max(1, FMath::CeilToInt(Length / GuideSampleSpacingCm));
		for (int32 PointIndex = 0; PointIndex <= SegmentCount; ++PointIndex)
		{
			const double Alpha = static_cast<double>(PointIndex) / SegmentCount;
			Guide.Points.Add(MakeGuidePoint(Start + (End - Start) * Alpha, SurfaceZCm, HalfWidthCm, SpeedCmPerSec));
		}
		return Guide;
	}

	FTDFluidFlowGuide MakeBendingWideningGuide(const FVector2D& Origin)
	{
		FTDFluidFlowGuide Guide;
		constexpr int32 PointCount = 61;
		for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			const double Alpha = static_cast<double>(PointIndex) / (PointCount - 1);
			const double X = -2200.0 + 4400.0 * Alpha;
			const double Y = 380.0 * FMath::Sin(X / 520.0);
			const float HalfWidthCm = static_cast<float>(FMath::Lerp(250.0, 450.0, Alpha));
			const float SpeedCmPerSec = static_cast<float>(FMath::Lerp(320.0, 180.0, Alpha));
			const float SurfaceZCm = static_cast<float>(40.0 - 35.0 * Alpha);
			Guide.Points.Add(MakeGuidePoint(Origin + FVector2D(X, Y), SurfaceZCm, HalfWidthCm, SpeedCmPerSec));
		}
		return Guide;
	}

	TArray<FTDFluidFlowGuide> MakeRasterGuides(const FVector2D& Origin)
	{
		TArray<FTDFluidFlowGuide> Guides;
		Guides.Add(MakeBendingWideningGuide(Origin));
		Guides.Add(MakeStraightGuide(Origin + FVector2D(-600.0, -1700.0), Origin + FVector2D(300.0, 200.0), 180.0f, 140.0f, 25.0f));
		return Guides;
	}

	TArray<FTDFluidFlowGuide> MakeRecollectedGuides(const FVector2D& Origin)
	{
		TArray<FTDFluidFlowGuide> Guides;
		Guides.Add(MakeStraightGuide(Origin + FVector2D(-2000.0, 900.0), Origin + FVector2D(2000.0, -300.0), 320.0f, 210.0f, 15.0f));
		return Guides;
	}

	TArray<FTDFluidWaterRegion> MakeRasterRegions(const FVector2D& Origin)
	{
		FTDFluidWaterRegion Region;
		Region.MinXY = Origin + FVector2D(700.0, -1500.0);
		Region.MaxXY = Origin + FVector2D(1600.0, -500.0);
		Region.SurfaceHeightCm = 12.5f;
		TArray<FTDFluidWaterRegion> Regions;
		Regions.Add(Region);
		return Regions;
	}

	ATDFluidLiveActor* SpawnContractLiveActor(UWorld& TargetWorld, const FVector& Location, const FTDFluidSimulationConfig& Config)
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

	AActor* SpawnBoxActor(UWorld& TargetWorld, const FVector& Center, const FVector& HalfExtentCm, bool bBlocksTerrainTraces, FName Tag)
	{
		AActor* BoxActor = TargetWorld.SpawnActor<AActor>(AActor::StaticClass(), Center, FRotator::ZeroRotator);
		if (!BoxActor)
		{
			return nullptr;
		}

		UBoxComponent* BoxComponent = NewObject<UBoxComponent>(BoxActor);
		BoxActor->AddInstanceComponent(BoxComponent);
		BoxActor->SetRootComponent(BoxComponent);
		BoxComponent->SetBoxExtent(HalfExtentCm);
		BoxComponent->SetMobility(EComponentMobility::Movable);
		if (bBlocksTerrainTraces)
		{
			BoxComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			BoxComponent->SetCollisionObjectType(ECC_WorldStatic);
			BoxComponent->SetCollisionResponseToAllChannels(ECR_Block);
		}
		else
		{
			BoxComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		BoxComponent->RegisterComponent();
		BoxActor->SetActorLocation(Center);
		if (!Tag.IsNone())
		{
			BoxActor->Tags.Add(Tag);
		}
		return BoxActor;
	}

	AActor* SpawnAnchorActor(UWorld& TargetWorld, const FVector& Location)
	{
		AActor* AnchorActor = TargetWorld.SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!AnchorActor)
		{
			return nullptr;
		}

		USceneComponent* AnchorRoot = NewObject<USceneComponent>(AnchorActor);
		AnchorActor->AddInstanceComponent(AnchorRoot);
		AnchorActor->SetRootComponent(AnchorRoot);
		AnchorRoot->RegisterComponent();
		AnchorActor->SetActorLocation(Location);
		return AnchorActor;
	}

	ATDFluidRiverActor* SpawnStraightRiver(UWorld& TargetWorld)
	{
		ATDFluidRiverActor* River = TargetWorld.SpawnActorDeferred<ATDFluidRiverActor>(ATDFluidRiverActor::StaticClass(), FTransform::Identity);
		if (!River)
		{
			return nullptr;
		}

		River->RiverWidthCm = RiverWidthCm;
		River->FlowSpeedCmPerSec = RiverFlowSpeedCmPerSec;
		River->GuideSampleSpacingCm = GuideSampleSpacingCm;
		River->bBuildRibbonSurface = false;
		USplineComponent* Spline = River->RiverSpline;
		Spline->ClearSplinePoints(false);
		Spline->AddSplinePoint(FVector(-RiverHalfLengthCm, 0.0, RiverSurfaceZCm), ESplineCoordinateSpace::World, false);
		Spline->AddSplinePoint(FVector(RiverHalfLengthCm, 0.0, RiverSurfaceZCm), ESplineCoordinateSpace::World, false);
		Spline->SetSplinePointType(0, ESplinePointType::Linear, false);
		Spline->SetSplinePointType(1, ESplinePointType::Linear, false);
		Spline->UpdateSpline();
		River->FinishSpawning(FTransform::Identity);
		return River;
	}

	bool SpawnRiverWaterScene(FAutomationTestBase& Test, const FString& Label, UWorld& TargetWorld, FTDWaterRiverScene& OutScene)
	{
		const FVector SlabCenter(0.0, 0.0, FlatBedElevationCm - TerrainSlabHalfHeightCm);
		if (!Test.TestNotNull(*(Label + TEXT(" spawned the terrain slab")), SpawnBoxActor(TargetWorld, SlabCenter, FVector(TerrainSlabHalfSizeCm, TerrainSlabHalfSizeCm, TerrainSlabHalfHeightCm), true, NAME_None)))
		{
			return false;
		}

		const FVector RockCenter(RockCenterXY.X, RockCenterXY.Y, 0.0);
		if (!Test.TestNotNull(*(Label + TEXT(" spawned the rock obstacle")), SpawnBoxActor(TargetWorld, RockCenter, FVector(RockHalfSizeCm, RockHalfSizeCm, 100.0), false, FName(TEXT("TDFluidObstacle")))))
		{
			return false;
		}

		OutScene.River = SpawnStraightRiver(TargetWorld);
		OutScene.Anchor = SpawnAnchorActor(TargetWorld, FVector::ZeroVector);
		if (!Test.TestNotNull(*(Label + TEXT(" spawned the river")), OutScene.River) || !Test.TestNotNull(*(Label + TEXT(" spawned the follow anchor")), OutScene.Anchor))
		{
			return false;
		}

		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(&TargetWorld, ETDFluidPresetKind::River);
		if (!Test.TestNotNull(*(Label + TEXT(" created the river preset")), Preset))
		{
			return false;
		}

		Preset->bOverridePresetDefaults = true;
		Preset->SimulationConfig.GridResolution = RiverSceneResolution;
		Preset->SimulationConfig.SimExtentCm = FVector2D(RiverSceneExtentCm, RiverSceneExtentCm);
		Preset->SimulationConfig.PressureIterations = RiverScenePressureIterations;
		Preset->SimulationConfig.SolverBackend = ETDFluidSolverBackend::Gpu;
		Preset->SimulationConfig.bEnableWaterLayer = true;

		OutScene.WaterActor = TargetWorld.SpawnActor<ATDFluidWaterActor>(ATDFluidWaterActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(*(Label + TEXT(" spawned the water actor")), OutScene.WaterActor) || !OutScene.WaterActor->LiveComponent)
		{
			return false;
		}

		OutScene.WaterActor->FollowTargetActor = OutScene.Anchor;
		OutScene.WaterActor->ViewLookAheadCm = 0.0f;
		OutScene.WaterActor->InitializeFromPreset(Preset, 0.0f);
		return true;
	}

	bool WaitForTerrainCompletion(FAutomationTestBase& Test, const FString& Label, FTDFluidGpuWaterContractTestWorld& TestWorld, const UTDFluidLiveComponent& Component)
	{
		for (int32 Tick = 1; Tick <= TerrainCompletionTickLimit; ++Tick)
		{
			TestWorld.StepWorld(ContractStepSeconds, true);
			if (Component.GetWaterLayerDisplayAlpha() > 0.0f)
			{
				ReportLine(Test, FString::Printf(TEXT("%s: terrain sampling completed after %d ticks"), *Label, Tick));
				return true;
			}
		}

		Test.AddError(MakeTerrainTimeoutMessage(Label, TerrainCompletionTickLimit));
		return false;
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

	bool CaptureGpuWaterOutputs(const UTDFluidLiveComponent& Component, FTDWaterOutputFrame& OutFrame)
	{
		return ReadRenderTargetPixels(Component.GetDensityPressureTexture(), OutFrame.DensityPressure)
			&& ReadRenderTargetPixels(Component.GetVelocityTexture(), OutFrame.Velocity)
			&& ReadRenderTargetPixels(Component.GetWaterSurfaceOutputTexture(), OutFrame.WaterSurface);
	}

	FVector2D DecodeVelocityPixel(const FFloat16Color& Pixel)
	{
		const float ReferenceSpeed = FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec();
		return FVector2D((Pixel.R.GetFloat() - 0.5f) * 2.0f * ReferenceSpeed, (Pixel.G.GetFloat() - 0.5f) * 2.0f * ReferenceSpeed);
	}

	float DecodeRipplePixel(const FFloat16Color& Pixel, float RippleHeightReferenceCm)
	{
		return (Pixel.B.GetFloat() - 0.5f) * 2.0f * RippleHeightReferenceCm;
	}

	bool IsClearVelocityPixel(const FFloat16Color& Pixel)
	{
		return Pixel.R.GetFloat() == 0.5f && Pixel.G.GetFloat() == 0.5f && Pixel.B.GetFloat() == 0.0f && Pixel.A.GetFloat() == 0.0f;
	}

	FIntPoint MapGridUVToProbeCell(const FVector2D& GridUV, int32 GridResolution)
	{
		const float GridX = GridUV.X * static_cast<float>(GridResolution) - 0.5f;
		const float GridY = GridUV.Y * static_cast<float>(GridResolution) - 0.5f;
		return FIntPoint(
			FMath::Clamp(FMath::RoundToInt(GridX), 0, GridResolution - 1),
			FMath::Clamp(FMath::RoundToInt(GridY), 0, GridResolution - 1));
	}

	bool MapPlanePointToComponentGridUV(const UTDFluidLiveComponent& Component, const FVector2D& PlanePoint, FVector2D& OutGridUV)
	{
		const FVector2D ExtentCm = Component.GetSimulationExtentCm();
		const FVector2D MinPlane = Component.GetSimulationCenterWorld() - 0.5f * ExtentCm;
		OutGridUV.X = (PlanePoint.X - MinPlane.X) / ExtentCm.X;
		OutGridUV.Y = (PlanePoint.Y - MinPlane.Y) / ExtentCm.Y;
		return OutGridUV.X >= 0.0f && OutGridUV.X <= 1.0f && OutGridUV.Y >= 0.0f && OutGridUV.Y <= 1.0f;
	}

	TArray<FTDWaterGuideCell> BuildStrongGuideCells(const UTDFluidLiveComponent& Component, const UTDFluidLiveSubsystem& FluidSubsystem)
	{
		const int32 GridResolution = Component.GetSimulationResolution();
		const FVector2D CenterPlane = Component.GetSimulationCenterWorld();
		const FVector2D ExtentCm = Component.GetSimulationExtentCm();
		TArray<FTDFluidFlowGuide> Guides;
		FluidSubsystem.CollectFlowGuidesInBounds(FBox2D(CenterPlane - ExtentCm, CenterPlane + ExtentCm), Guides);

		FTDFluidWaterInputRaster GuideRaster;
		GuideRaster.SetFlowGuides(Guides);
		GuideRaster.SetWaterRegions(TArray<FTDFluidWaterRegion>());
		const float BankFalloffCm = FMath::Clamp(Component.SimulationConfig.FlowGuideBankFalloffCm, 0.0f, 10000.0f);
		const float BankSpeedRatio = FMath::Clamp(Component.SimulationConfig.FlowGuideBankSpeedRatio, 0.0f, 1.0f);
		TArray<double> RowWorldY;
		RowWorldY.SetNumUninitialized(GridResolution);
		for (int32 CellY = 0; CellY < GridResolution; ++CellY)
		{
			RowWorldY[CellY] = Component.CellToPlaneCenter(0, CellY).Y;
		}
		GuideRaster.BuildRowSegmentLists(RowWorldY, 0.5 * ExtentCm.Y / GridResolution + BankFalloffCm);

		TArray<FTDWaterGuideCell> GuideCells;
		for (int32 CellY = 0; CellY < GridResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < GridResolution; ++CellX)
			{
				const FTDFluidWaterRasterCell RasterCell = GuideRaster.RasterizeCell(Component.CellToPlaneCenter(CellX, CellY), CellY, BankFalloffCm, BankSpeedRatio, FTDFluidGrid2DSolver::NoWaterSurfaceHeight);
				if (RasterCell.GuideWeight < StrongGuideWeight)
				{
					continue;
				}

				FTDWaterGuideCell& GuideCell = GuideCells.AddDefaulted_GetRef();
				GuideCell.CellIndex = CellY * GridResolution + CellX;
				GuideCell.TargetVelocityCmPerSec = FVector2D(RasterCell.TargetVelocityX, RasterCell.TargetVelocityY);
				GuideCell.GuideWeight = RasterCell.GuideWeight;
			}
		}
		return GuideCells;
	}

	FTDWaterFlowActivity MeasureFlowActivity(const TArray<FTDWaterGuideCell>& GuideCells, const FTDWaterOutputFrame& Frame)
	{
		FTDWaterFlowActivity Activity;
		if (GuideCells.IsEmpty() || Frame.Velocity.IsEmpty() || Frame.WaterSurface.Num() != Frame.Velocity.Num())
		{
			return Activity;
		}

		int32 WetCellCount = 0;
		double AlongTargetSum = 0.0;
		double TargetSum = 0.0;
		for (const FTDWaterGuideCell& GuideCell : GuideCells)
		{
			if (Frame.WaterSurface[GuideCell.CellIndex].R.GetFloat() > 0.0f)
			{
				++WetCellCount;
			}

			const double TargetSpeed = GuideCell.TargetVelocityCmPerSec.Size();
			if (TargetSpeed <= UE_DOUBLE_SMALL_NUMBER)
			{
				continue;
			}

			const FVector2D Velocity = DecodeVelocityPixel(Frame.Velocity[GuideCell.CellIndex]);
			AlongTargetSum += FVector2D::DotProduct(Velocity, GuideCell.TargetVelocityCmPerSec / TargetSpeed);
			TargetSum += TargetSpeed * GuideCell.GuideWeight;
		}

		int32 ChangedVelocityCount = 0;
		for (const FFloat16Color& Pixel : Frame.Velocity)
		{
			ChangedVelocityCount += IsClearVelocityPixel(Pixel) ? 0 : 1;
		}

		Activity.WetFraction = static_cast<double>(WetCellCount) / GuideCells.Num();
		Activity.MeanAlongTargetCmPerSec = AlongTargetSum / GuideCells.Num();
		Activity.MeanTargetCmPerSec = TargetSum / GuideCells.Num();
		Activity.ChangedVelocityFraction = static_cast<double>(ChangedVelocityCount) / Frame.Velocity.Num();
		return Activity;
	}

	bool PassesFlowActivity(const FTDWaterFlowActivity& Activity)
	{
		return Activity.WetFraction >= FlowWetFractionThreshold
			&& Activity.MeanAlongTargetCmPerSec >= FlowSpeedRatioThreshold * Activity.MeanTargetCmPerSec
			&& Activity.ChangedVelocityFraction >= FlowChangedFractionThreshold;
	}

	FString DescribeFlowActivity(const FTDWaterFlowActivity& Activity)
	{
		return FString::Printf(TEXT("wet=%.3f along=%.2f target=%.2f changed=%.3f"), Activity.WetFraction, Activity.MeanAlongTargetCmPerSec, Activity.MeanTargetCmPerSec, Activity.ChangedVelocityFraction);
	}

	TArray<int32> CollectDryBandCells(const UTDFluidLiveComponent& Component)
	{
		TArray<int32> DryCells;
		const int32 GridResolution = Component.GetSimulationResolution();
		for (int32 CellY = 0; CellY < GridResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < GridResolution; ++CellX)
			{
				const FVector2D CellCenter = Component.CellToPlaneCenter(CellX, CellY);
				const double OffsetCm = FMath::Abs(CellCenter.Y);
				if (OffsetCm >= DryBandMinimumOffsetCm && OffsetCm <= DryBandMaximumOffsetCm && FMath::Abs(CellCenter.X) <= DryBandHalfLengthCm)
				{
					DryCells.Add(CellY * GridResolution + CellX);
				}
			}
		}
		return DryCells;
	}

	int32 CountOpenDryCells(const TArray<int32>& DryCells, const FTDWaterOutputFrame& Frame)
	{
		int32 OpenCount = 0;
		for (const int32 CellIndex : DryCells)
		{
			if (!Frame.DensityPressure.IsValidIndex(CellIndex) || Frame.DensityPressure[CellIndex].A.GetFloat() != 0.0f)
			{
				++OpenCount;
			}
		}
		return OpenCount;
	}

	TArray<FVector> MakeRiverProbePoints()
	{
		TArray<FVector> Points;
		Points.Add(FVector(5000.0, 0.0, 0.0));
		Points.Add(FVector(0.0, -5000.0, 0.0));
		Points.Add(FVector(RockCenterXY.X, RockCenterXY.Y, 0.0));
		Points.Add(FVector(-300.0, 1550.0, 0.0));
		const double ProbeYs[] = { -900.0, -300.0, 300.0, 900.0 };
		for (const double ProbeY : ProbeYs)
		{
			for (int32 Column = 0; Column < 15; ++Column)
			{
				Points.Add(FVector(-1400.0 + 200.0 * Column + 7.0, ProbeY + 3.0 * Column, 0.0));
			}
		}
		return Points;
	}

	void CompareProbeSamplesWithOutputs(
		const UTDFluidLiveComponent& Component,
		const TArray<FVector>& Points,
		const TArray<FTDFluidWaterPointSample>& Samples,
		const TMap<uint32, FTDWaterOutputFrame>& Frames,
		uint32 LastFrameNumber,
		FTDProbeComparisonTally& InOutTally)
	{
		const int32 GridResolution = Component.GetSimulationResolution();
		const float DepthReferenceCm = Component.SimulationConfig.WaterDepthReferenceCm;
		const float RippleReferenceCm = Component.SimulationConfig.RippleHeightReferenceCm;
		const uint32 ReadbackFieldFrame = Component.GetGpuReadbackField().FrameNumber;
		const auto RecordMismatch = [&InOutTally](const FString& Description)
		{
			++InOutTally.MismatchCount;
			if (InOutTally.FirstMismatch.IsEmpty())
			{
				InOutTally.FirstMismatch = Description;
			}
		};

		for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
		{
			const FTDFluidWaterPointSample& PointSample = Samples[SampleIndex];
			if (!Points.IsValidIndex(SampleIndex) || !PointSample.WorldLocation.Equals(Points[SampleIndex], 0.0))
			{
				++InOutTally.LocationMismatchCount;
				continue;
			}

			FVector2D GridUV;
			const bool bIsExpectedInside = MapPlanePointToComponentGridUV(Component, Component.ProjectWorldToSolverPlane(Points[SampleIndex]), GridUV);
			if (bIsExpectedInside != PointSample.bIsInsideWindow)
			{
				++InOutTally.InsideFlagMismatchCount;
				continue;
			}

			if (!bIsExpectedInside)
			{
				continue;
			}

			const uint32 SampleFrame = LastFrameNumber - static_cast<uint32>(PointSample.FrameAge);
			if (PointSample.FrameAge < 0 || PointSample.FrameAge > MaximumProbeFrameAge)
			{
				if (ReadbackFieldFrame == SampleFrame)
				{
					++InOutTally.DroppedRingFrameCount;
				}
				else
				{
					++InOutTally.FrameAgeViolationCount;
				}
				continue;
			}

			const FTDWaterOutputFrame* Frame = Frames.Find(SampleFrame);
			if (!Frame)
			{
				++InOutTally.MissingFrameCount;
				continue;
			}

			const FIntPoint Cell = MapGridUVToProbeCell(GridUV, GridResolution);
			const int32 CellIndex = Cell.Y * GridResolution + Cell.X;
			if (!Frame->DensityPressure.IsValidIndex(CellIndex) || !Frame->Velocity.IsValidIndex(CellIndex) || !Frame->WaterSurface.IsValidIndex(CellIndex))
			{
				++InOutTally.MissingFrameCount;
				continue;
			}

			++InOutTally.ComparedSampleCount;
			const FFloat16Color& DensityPixel = Frame->DensityPressure[CellIndex];
			const FFloat16Color& VelocityPixel = Frame->Velocity[CellIndex];
			const FFloat16Color& SurfacePixel = Frame->WaterSurface[CellIndex];
			const FString Where = FString::Printf(TEXT("probe %d cell (%d, %d) frame %u age %d"), SampleIndex, Cell.X, Cell.Y, SampleFrame, PointSample.FrameAge);

			const FVector2D OutputVelocity = DecodeVelocityPixel(VelocityPixel);
			const float SaturatedSpeed = FTDFluidGrid2DSolver::GetOutputSpeedReferenceCmPerSec() * (1.0f - SaturationMargin);
			if (FMath::Abs(OutputVelocity.X) < SaturatedSpeed && FMath::Abs(PointSample.VelocityCmPerSec.X - OutputVelocity.X) > ProbeVelocityToleranceCmPerSec)
			{
				RecordMismatch(FString::Printf(TEXT("%s velocity x %.4f vs output %.4f"), *Where, PointSample.VelocityCmPerSec.X, OutputVelocity.X));
			}
			if (FMath::Abs(OutputVelocity.Y) < SaturatedSpeed && FMath::Abs(PointSample.VelocityCmPerSec.Y - OutputVelocity.Y) > ProbeVelocityToleranceCmPerSec)
			{
				RecordMismatch(FString::Printf(TEXT("%s velocity y %.4f vs output %.4f"), *Where, PointSample.VelocityCmPerSec.Y, OutputVelocity.Y));
			}

			const float DepthRatio = SurfacePixel.R.GetFloat();
			if (DepthRatio < 1.0f - SaturationMargin && FMath::Abs(PointSample.WaterDepthCm - DepthRatio * DepthReferenceCm) > ProbeDepthToleranceCm)
			{
				RecordMismatch(FString::Printf(TEXT("%s depth %.4f vs output %.4f"), *Where, PointSample.WaterDepthCm, DepthRatio * DepthReferenceCm));
			}

			const float OutputWhitewater = DensityPixel.R.GetFloat();
			if (OutputWhitewater < 1.0f - SaturationMargin && FMath::Abs(PointSample.Whitewater - OutputWhitewater) > ProbeWhitewaterTolerance)
			{
				RecordMismatch(FString::Printf(TEXT("%s whitewater %.6f vs output %.6f"), *Where, PointSample.Whitewater, OutputWhitewater));
			}

			const float RippleOffset = FMath::Abs(SurfacePixel.B.GetFloat() - 0.5f);
			const float OutputRippleCm = DecodeRipplePixel(SurfacePixel, RippleReferenceCm);
			if (RippleOffset < 0.5f - SaturationMargin && FMath::Abs(PointSample.RippleHeightCm - OutputRippleCm) > ProbeRippleToleranceCm)
			{
				RecordMismatch(FString::Printf(TEXT("%s ripple %.5f vs output %.5f"), *Where, PointSample.RippleHeightCm, OutputRippleCm));
			}

			const bool bIsProbeSolid = PointSample.ObstacleSdfCm <= 0.0f;
			const bool bIsOutputSolid = DensityPixel.A.GetFloat() == 0.0f;
			if (bIsProbeSolid != bIsOutputSolid)
			{
				RecordMismatch(FString::Printf(TEXT("%s solid probe=%d output=%d sdf=%.3f"), *Where, bIsProbeSolid ? 1 : 0, bIsOutputSolid ? 1 : 0, PointSample.ObstacleSdfCm));
			}
		}
	}

	void PruneOutputFrames(TMap<uint32, FTDWaterOutputFrame>& Frames, uint32 LastFrameNumber)
	{
		TArray<uint32> StaleFrames;
		for (const TPair<uint32, FTDWaterOutputFrame>& Entry : Frames)
		{
			if (Entry.Key + StoredOutputFrameCount < LastFrameNumber)
			{
				StaleFrames.Add(Entry.Key);
			}
		}
		for (const uint32 StaleFrame : StaleFrames)
		{
			Frames.Remove(StaleFrame);
		}
	}

	bool StepAndCaptureFrame(FTDFluidGpuWaterContractTestWorld& TestWorld, const UTDFluidLiveComponent& Component, const UTDFluidLiveSubsystem& FluidSubsystem, TMap<uint32, FTDWaterOutputFrame>& Frames)
	{
		TestWorld.StepWorld(ContractStepSeconds, true);
		const uint32 LastFrameNumber = FluidSubsystem.GetLastGpuSolverFrameNumber();
		FTDWaterOutputFrame& Frame = Frames.FindOrAdd(LastFrameNumber);
		const bool bHasCaptured = CaptureGpuWaterOutputs(Component, Frame);
		PruneOutputFrames(Frames, LastFrameNumber);
		return bHasCaptured;
	}

	bool RunProbeComparisonPhase(
		FAutomationTestBase& Test,
		const FString& Label,
		FTDFluidGpuWaterContractTestWorld& TestWorld,
		UTDFluidLiveComponent& Component,
		const UTDFluidLiveSubsystem& FluidSubsystem,
		const TArray<FVector>& Points,
		TMap<uint32, FTDWaterOutputFrame>& Frames)
	{
		Component.SetWaterProbeLocations(Points);
		const int32 ExpectedSampleCount = FMath::Min(Points.Num(), TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame);
		TArray<FTDFluidWaterPointSample> Samples;
		int32 GenerationArrivalFrame = INDEX_NONE;
		for (int32 FrameIndex = 1; FrameIndex <= ProbeGenerationFrameLimit; ++FrameIndex)
		{
			StepAndCaptureFrame(TestWorld, Component, FluidSubsystem, Frames);
			if (Component.GetWaterProbeSamples(Samples) && Samples.Num() == ExpectedSampleCount)
			{
				GenerationArrivalFrame = FrameIndex;
				break;
			}
		}

		if (!Test.TestTrue(*FString::Printf(TEXT("%s: the new probe generation arrives within %d frames"), *Label, ProbeGenerationFrameLimit), GenerationArrivalFrame != INDEX_NONE))
		{
			return false;
		}

		FTDProbeComparisonTally Tally;
		int32 CaptureFailureCount = 0;
		for (int32 FrameIndex = 0; FrameIndex < ProbeComparisonFrameCount; ++FrameIndex)
		{
			CaptureFailureCount += StepAndCaptureFrame(TestWorld, Component, FluidSubsystem, Frames) ? 0 : 1;
			if (!Component.GetWaterProbeSamples(Samples))
			{
				++Tally.MissingFrameCount;
				continue;
			}

			Test.TestEqual(*FString::Printf(TEXT("%s: one sample per registered point"), *Label), static_cast<int32>(Samples.Num()), static_cast<int32>(ExpectedSampleCount));
			CompareProbeSamplesWithOutputs(Component, Points, Samples, Frames, FluidSubsystem.GetLastGpuSolverFrameNumber(), Tally);
		}

		ReportLine(Test, FString::Printf(TEXT("%s: arrival_frame=%d compared=%d mismatches=%d missing=%d age_violations=%d dropped_ring_frames=%d inside_flag_mismatches=%d location_mismatches=%d first=%s"),
			*Label, GenerationArrivalFrame, Tally.ComparedSampleCount, Tally.MismatchCount, Tally.MissingFrameCount, Tally.FrameAgeViolationCount, Tally.DroppedRingFrameCount, Tally.InsideFlagMismatchCount, Tally.LocationMismatchCount, *Tally.FirstMismatch));
		Test.TestEqual(*FString::Printf(TEXT("%s: every GPU output frame was read back"), *Label), static_cast<int32>(CaptureFailureCount), 0);
		Test.TestTrue(*FString::Printf(TEXT("%s: probe samples were compared"), *Label), Tally.ComparedSampleCount > 0);
		Test.TestEqual(*FString::Printf(TEXT("%s: probe samples match the output render targets (%s)"), *Label, *Tally.FirstMismatch), static_cast<int32>(Tally.MismatchCount), 0);
		Test.TestEqual(*FString::Printf(TEXT("%s: every sample frame age is at most %d"), *Label, MaximumProbeFrameAge), static_cast<int32>(Tally.FrameAgeViolationCount), 0);
		Test.TestEqual(*FString::Printf(TEXT("%s: every sampled frame has stored outputs"), *Label), static_cast<int32>(Tally.MissingFrameCount), 0);
		Test.TestEqual(*FString::Printf(TEXT("%s: window membership matches the probe flags"), *Label), static_cast<int32>(Tally.InsideFlagMismatchCount), 0);
		Test.TestEqual(*FString::Printf(TEXT("%s: samples keep the registration order"), *Label), static_cast<int32>(Tally.LocationMismatchCount), 0);
		return Tally.MismatchCount == 0;
	}

	FTDFluidGpuActorInput GatherProxyStep(FTDFluidGpuSolverProxy& Proxy, uint32 FrameNumber, float DeltaTime)
	{
		Proxy.PrepareStep(DeltaTime, TConstArrayView<FTDFluidBrushStamp>(), FVector2D::ZeroVector, true);
		FTDFluidGpuActorInput ActorInput;
		Proxy.GatherStep(FrameNumber, 0, nullptr, nullptr, ActorInput);
		return ActorInput;
	}

	bool IsFullWaterUpload(const FTDFluidGpuActorInput& ActorInput, int32 FieldResolution)
	{
		const int32 CellCount = FieldResolution * FieldResolution;
		return ActorInput.WaterGuideUpdates.Num() == CellCount && ActorInput.WaterTerrainUpdates.Num() == CellCount;
	}

	int32 CountUnknownTerrainUpdates(const FTDFluidGpuActorInput& ActorInput)
	{
		int32 UnknownCount = 0;
		for (const FTDFluidGpuTerrainCellUpdate& Update : ActorInput.WaterTerrainUpdates)
		{
			UnknownCount += Update.ElevationCm <= TDFluidGpuSolverConstants::TerrainUnknownElevationThresholdCm ? 1 : 0;
		}
		return UnknownCount;
	}

	bool FindTerrainUpdate(const FTDFluidGpuActorInput& ActorInput, const FIntPoint& Cell, float& OutElevationCm)
	{
		for (const FTDFluidGpuTerrainCellUpdate& Update : ActorInput.WaterTerrainUpdates)
		{
			if (Update.Cell == Cell)
			{
				OutElevationCm = Update.ElevationCm;
				return true;
			}
		}
		return false;
	}

	int32 CountGuidedUpdates(const FTDFluidGpuActorInput& ActorInput)
	{
		int32 GuidedCount = 0;
		for (const FTDFluidGpuWaterGuideCellUpdate& Update : ActorInput.WaterGuideUpdates)
		{
			GuidedCount += Update.GuideWeight > 0.0f ? 1 : 0;
		}
		return GuidedCount;
	}

	int32 CountNewShiftCells(int32 GridSize, const FIntPoint& Shift)
	{
		int32 NewCellCount = 0;
		for (int32 CellY = 0; CellY < GridSize; ++CellY)
		{
			for (int32 CellX = 0; CellX < GridSize; ++CellX)
			{
				const int32 SourceX = CellX + Shift.X;
				const int32 SourceY = CellY + Shift.Y;
				NewCellCount += (SourceX < 0 || SourceX >= GridSize || SourceY < 0 || SourceY >= GridSize) ? 1 : 0;
			}
		}
		return NewCellCount;
	}

	FTDFluidWaterInputRaster MakeIndependentRaster(const TArray<FTDFluidFlowGuide>& Guides, const TArray<FTDFluidWaterRegion>& Regions, TConstArrayView<double> RowWorldY, double RowSlackCm)
	{
		FTDFluidWaterInputRaster IndependentRaster;
		IndependentRaster.SetFlowGuides(Guides);
		IndependentRaster.SetWaterRegions(Regions);
		IndependentRaster.BuildRowSegmentLists(RowWorldY, RowSlackCm);
		return IndependentRaster;
	}

	int32 CompareFieldUploadWithSolver(
		const FTDFluidGpuActorInput& FullUpload,
		const FTDFluidGpuWaterFieldState& FieldState,
		const FTDFluidGrid2DSolver& Solver,
		const TArray<FTDFluidFlowGuide>& Guides,
		const TArray<FTDFluidWaterRegion>& Regions,
		FString& OutFirstMismatch)
	{
		const int32 GridSize = Solver.GetResolution();
		const FTDFluidSimulationConfig& SolverConfig = Solver.GetConfig();
		TArray<double> RowWorldY;
		RowWorldY.SetNumUninitialized(GridSize);
		for (int32 CellY = 0; CellY < GridSize; ++CellY)
		{
			RowWorldY[CellY] = Solver.CellToWorldCenter(0, CellY).Y;
		}
		const FTDFluidWaterInputRaster IndependentRaster = MakeIndependentRaster(Guides, Regions, RowWorldY, 0.5 * SolverConfig.SimExtentCm.Y / GridSize + SolverConfig.FlowGuideBankFalloffCm);

		int32 MismatchCount = 0;
		const FTDWaterContractDenormalScope DenormalScope;
		for (const FTDFluidGpuWaterGuideCellUpdate& Update : FullUpload.WaterGuideUpdates)
		{
			const int32 CellX = Update.FieldCell.X;
			const int32 CellY = Update.FieldCell.Y;
			const FTDFluidCellSample CellSample = Solver.SampleAtCell(CellX, CellY);
			const float SolverSurfaceCm = Solver.GetWaterSurfaceHeightAtCell(CellX, CellY);
			const float ExpectedUploadSurfaceCm = SolverSurfaceCm == FTDFluidGrid2DSolver::NoWaterSurfaceHeight ? TDFluidGpuSolverConstants::WaterNoSurfaceHeightCm : SolverSurfaceCm;
			const FTDFluidWaterRasterCell IndependentCell = IndependentRaster.RasterizeCell(Solver.CellToWorldCenter(CellX, CellY), CellY, SolverConfig.FlowGuideBankFalloffCm, SolverConfig.FlowGuideBankSpeedRatio, FTDFluidGrid2DSolver::NoWaterSurfaceHeight);
			const bool bMatches = AreFloatBitsEqual(FieldState.GetSurfaceHeightAtFieldCell(CellX, CellY), SolverSurfaceCm)
				&& AreFloatBitsEqual(Update.SurfaceHeightCm, ExpectedUploadSurfaceCm)
				&& AreFloatBitsEqual(Update.TargetVelocityCmPerSec.X, static_cast<float>(CellSample.FlowTargetVelocity.X))
				&& AreFloatBitsEqual(Update.TargetVelocityCmPerSec.Y, static_cast<float>(CellSample.FlowTargetVelocity.Y))
				&& AreFloatBitsEqual(Update.GuideWeight, CellSample.FlowGuideWeight)
				&& AreFloatBitsEqual(Update.GuideDescent, IndependentCell.GuideDescent);
			if (bMatches)
			{
				continue;
			}

			++MismatchCount;
			if (OutFirstMismatch.IsEmpty())
			{
				OutFirstMismatch = FString::Printf(TEXT("cell (%d, %d) field S=%.6f solver S=%.6f upload T=(%.6f, %.6f) solver T=(%.6f, %.6f) W=%.6f/%.6f D=%.6f/%.6f"),
					CellX, CellY, FieldState.GetSurfaceHeightAtFieldCell(CellX, CellY), SolverSurfaceCm,
					Update.TargetVelocityCmPerSec.X, Update.TargetVelocityCmPerSec.Y, CellSample.FlowTargetVelocity.X, CellSample.FlowTargetVelocity.Y,
					Update.GuideWeight, CellSample.FlowGuideWeight, Update.GuideDescent, IndependentCell.GuideDescent);
			}
		}
		return MismatchCount;
	}

	FTDFluidGpuActorInput TakeFullFieldUpload(FTDFluidGpuWaterFieldState& FieldState)
	{
		FieldState.RasterizeDirtyCells();
		FieldState.RequestFullUpload(1);
		FTDFluidGpuActorInput ActorInput;
		FieldState.MoveUpdatesInto(ActorInput);
		return ActorInput;
	}

	bool ExpectFieldMatchesSolver(FAutomationTestBase& Test, const FString& Label, FTDFluidGpuWaterFieldState& FieldState, const FTDFluidGrid2DSolver& Solver, const TArray<FTDFluidFlowGuide>& Guides, const TArray<FTDFluidWaterRegion>& Regions)
	{
		const FTDFluidGpuActorInput FullUpload = TakeFullFieldUpload(FieldState);
		const int32 CellCount = Solver.GetResolution() * Solver.GetResolution();
		if (!Test.TestEqual(*(Label + TEXT(" full upload carries every field cell")), static_cast<int32>(FullUpload.WaterGuideUpdates.Num()), static_cast<int32>(CellCount)))
		{
			return false;
		}

		FString FirstMismatch;
		const int32 MismatchCount = CompareFieldUploadWithSolver(FullUpload, FieldState, Solver, Guides, Regions, FirstMismatch);
		ReportLine(Test, FString::Printf(TEXT("%s: mismatches=%d first=%s"), *Label, MismatchCount, *FirstMismatch));
		return Test.TestEqual(*FString::Printf(TEXT("%s field S, T, W and D are bit identical to the solver raster (%s)"), *Label, *FirstMismatch), static_cast<int32>(MismatchCount), 0);
	}

	bool ShiftSolverWindowExactly(FTDFluidGrid2DSolver& Solver, const FIntPoint& Shift)
	{
		const float CellSizeX = Solver.GetExtentCm().X / static_cast<float>(Solver.GetResolution());
		const float CellSizeY = Solver.GetExtentCm().Y / static_cast<float>(Solver.GetResolution());
		const FVector2D DesiredCenter = Solver.GetCenterWorld() + FVector2D(static_cast<float>(Shift.X) * CellSizeX, static_cast<float>(Shift.Y) * CellSizeY);
		return Solver.ShiftOriginForSlidingWindow(DesiredCenter);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterContractProbeSamplesUseCpuSolverDirectlyTest,
	"TDGame.FluidFX.Water.Gpu.ProbeSamplesUseCpuSolverDirectly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterContractProbeSamplesUseCpuSolverDirectlyTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterContractTestPrivate;

	const FString Label(TEXT("H6 ProbeSamplesUseCpuSolverDirectly"));
	FTDWaterGpuConsoleVariableScope BackendScope(SolverBackendVariableName, 0);
	FTDFluidGpuWaterContractTestWorld TestWorld;
	FTDFluidSimulationConfig Config = MakeContractWaterConfig(64);
	Config.SolverBackend = ETDFluidSolverBackend::Cpu;
	ATDFluidLiveActor* LiveActor = SpawnContractLiveActor(*TestWorld.World, FVector::ZeroVector, Config);
	if (!TestNotNull(*(Label + TEXT(" spawned the CPU water actor")), LiveActor))
	{
		return false;
	}

	UTDFluidLiveComponent& Component = *LiveActor->LiveComponent;
	if (!TestFalse(*(Label + TEXT(" water actor runs on the CPU solver")), Component.IsUsingGpuSolver()))
	{
		return false;
	}

	TArray<FTDFluidFlowGuide> Guides;
	Guides.Add(MakeStraightGuide(FVector2D(-800.0, 0.0), FVector2D(800.0, 0.0), 200.0f, 150.0f, 0.0f));
	Component.SetWaterGuidesAndRegions(Guides, TArray<FTDFluidWaterRegion>());
	const int32 Resolution = Component.GetSimulationResolution();
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			Component.SetTerrainElevationAtCell(CellX, CellY, FlatBedElevationCm);
		}
	}
	Component.CompleteTerrainElevationSampling();
	for (int32 StepIndex = 0; StepIndex < 20; ++StepIndex)
	{
		TestWorld.StepWorld(ContractStepSeconds, false);
	}

	TArray<FVector> Points;
	Points.Add(FVector(0.0, 0.0, 0.0));
	Points.Add(FVector(100.0, 50.0, 0.0));
	Points.Add(FVector(-250.0, -120.0, 0.0));
	Points.Add(FVector(300.0, 400.0, 0.0));
	Points.Add(FVector(2000.0, 0.0, 0.0));
	Points.Add(FVector(-590.0, 590.0, 0.0));
	Component.SetWaterProbeLocations(Points);

	TArray<FTDFluidWaterPointSample> Samples;
	if (!TestTrue(*(Label + TEXT(" CPU probe samples are available")), Component.GetWaterProbeSamples(Samples)))
	{
		return false;
	}
	if (!TestEqual(*(Label + TEXT(" one sample per point")), static_cast<int32>(Samples.Num()), static_cast<int32>(Points.Num())))
	{
		return false;
	}

	const FTDFluidGrid2DSolver& Solver = Component.GetSolver();
	int32 InsideCount = 0;
	int32 DrySurfaceCount = 0;
	for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
	{
		const FTDFluidWaterPointSample& PointSample = Samples[PointIndex];
		const FString PointLabel = FString::Printf(TEXT("%s point %d"), *Label, PointIndex);
		TestTrue(*(PointLabel + TEXT(" keeps its world location")), PointSample.WorldLocation.Equals(Points[PointIndex], 0.0));
		TestEqual(*(PointLabel + TEXT(" is read directly with frame age 0")), static_cast<int32>(PointSample.FrameAge), 0);

		FVector2D GridUV;
		const bool bIsInside = Solver.WorldToGridUV(Component.ProjectWorldToSolverPlane(Points[PointIndex]), GridUV);
		TestEqual(*(PointLabel + TEXT(" window membership")), PointSample.bIsInsideWindow, bIsInside);
		if (!bIsInside)
		{
			continue;
		}

		++InsideCount;
		const FIntPoint Cell = MapGridUVToProbeCell(GridUV, Solver.GetResolution());
		const FTDFluidCellSample CellSample = Solver.SampleAtCell(Cell.X, Cell.Y);
		const float SolverSurfaceCm = Solver.GetWaterSurfaceHeightAtCell(Cell.X, Cell.Y);
		const bool bHasSolverSurface = SolverSurfaceCm != FTDFluidGrid2DSolver::NoWaterSurfaceHeight;
		DrySurfaceCount += bHasSolverSurface ? 0 : 1;
		TestEqual(*(PointLabel + TEXT(" surface flag follows the solver marker")), PointSample.bHasWaterSurface, bHasSolverSurface);
		TestTrue(*(PointLabel + TEXT(" surface height is the solver value or 0 without a surface")), AreFloatBitsEqual(PointSample.WaterSurfaceHeightCm, bHasSolverSurface ? SolverSurfaceCm : 0.0f));
		TestEqual(*(PointLabel + TEXT(" terrain known flag")), PointSample.bIsTerrainKnown, CellSample.bIsTerrainKnown);
		TestTrue(*(PointLabel + TEXT(" depth is bit identical")), AreFloatBitsEqual(PointSample.WaterDepthCm, CellSample.WaterDepthCm));
		TestTrue(*(PointLabel + TEXT(" velocity is bit identical")), AreDoubleBitsEqual(PointSample.VelocityCmPerSec.X, CellSample.VelocityCmPerSec.X) && AreDoubleBitsEqual(PointSample.VelocityCmPerSec.Y, CellSample.VelocityCmPerSec.Y));
		TestTrue(*(PointLabel + TEXT(" target velocity is bit identical")), AreDoubleBitsEqual(PointSample.FlowTargetVelocityCmPerSec.X, CellSample.FlowTargetVelocity.X) && AreDoubleBitsEqual(PointSample.FlowTargetVelocityCmPerSec.Y, CellSample.FlowTargetVelocity.Y));
		TestTrue(*(PointLabel + TEXT(" guide weight is bit identical")), AreFloatBitsEqual(PointSample.FlowGuideWeight, CellSample.FlowGuideWeight));
		TestTrue(*(PointLabel + TEXT(" whitewater is the primary density")), AreFloatBitsEqual(PointSample.Whitewater, CellSample.PrimaryDensity));
		TestTrue(*(PointLabel + TEXT(" ripple height is bit identical")), AreFloatBitsEqual(PointSample.RippleHeightCm, CellSample.RippleHeightCm));
		TestTrue(*(PointLabel + TEXT(" obstacle distance is bit identical")), AreFloatBitsEqual(PointSample.ObstacleSdfCm, CellSample.ObstacleSdf));
		TestTrue(*(PointLabel + TEXT(" wetness is bit identical")), AreFloatBitsEqual(PointSample.Wetness, CellSample.Wetness));
	}
	TestEqual(*(Label + TEXT(" five points are inside the window")), static_cast<int32>(InsideCount), 5);
	TestTrue(*(Label + TEXT(" the dry point has no water surface")), DrySurfaceCount >= 1);
	TestFalse(*(Label + TEXT(" the dry bank point reports no surface")), Samples[3].bHasWaterSurface);
	TestTrue(*(Label + TEXT(" the dry bank point reports height 0")), AreFloatBitsEqual(Samples[3].WaterSurfaceHeightCm, 0.0f));
	TestFalse(*(Label + TEXT(" the far point is outside the window")), Samples[4].bIsInsideWindow);

	TArray<FVector> ManyPoints;
	for (int32 PointIndex = 0; PointIndex < TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame + 1; ++PointIndex)
	{
		ManyPoints.Add(FVector(-500.0 + 15.0 * PointIndex, 10.0, 0.0));
	}
	Component.SetWaterProbeLocations(ManyPoints);
	TArray<FTDFluidWaterPointSample> ManySamples;
	TestTrue(*(Label + TEXT(" samples after registering 65 points")), Component.GetWaterProbeSamples(ManySamples));
	TestEqual(*(Label + TEXT(" only the first 64 points are kept")), static_cast<int32>(ManySamples.Num()), static_cast<int32>(TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame));

	TArray<FVector> ReorderedPoints(Points);
	Algo::Reverse(ReorderedPoints);
	Component.SetWaterProbeLocations(ReorderedPoints);
	TArray<FTDFluidWaterPointSample> ReorderedSamples;
	TestTrue(*(Label + TEXT(" samples after a new registration")), Component.GetWaterProbeSamples(ReorderedSamples));
	if (!TestEqual(*(Label + TEXT(" new registration keeps its count")), static_cast<int32>(ReorderedSamples.Num()), static_cast<int32>(Points.Num())))
	{
		return false;
	}

	for (int32 PointIndex = 0; PointIndex < ReorderedPoints.Num(); ++PointIndex)
	{
		const FTDFluidWaterPointSample& Reordered = ReorderedSamples[PointIndex];
		const FTDFluidWaterPointSample& Original = Samples[Points.Num() - 1 - PointIndex];
		TestTrue(*FString::Printf(TEXT("%s reordered sample %d follows the new order"), *Label, PointIndex), Reordered.WorldLocation.Equals(ReorderedPoints[PointIndex], 0.0) && AreDoubleBitsEqual(Reordered.VelocityCmPerSec.X, Original.VelocityCmPerSec.X) && AreFloatBitsEqual(Reordered.WaterDepthCm, Original.WaterDepthCm));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterContractFieldRasterMatchesSolverRasterTest,
	"TDGame.FluidFX.Water.Gpu.FieldRasterMatchesSolverRaster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterContractFieldRasterMatchesSolverRasterTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterContractTestPrivate;

	constexpr int32 Resolution = 192;
	const FVector2D WindowCenter(310.0, -145.0);
	const FVector2D ExtentCases[] = { FVector2D(Resolution * SquareCellSizeCm, Resolution * SquareCellSizeCm), FVector2D(3500.0, 3300.0) };
	const FIntPoint Shifts[] = { FIntPoint(2, -1), FIntPoint(0, 3), FIntPoint(-5, 0) };
	const TArray<FTDFluidFlowGuide> Guides = MakeRasterGuides(WindowCenter);
	const TArray<FTDFluidFlowGuide> RecollectedGuides = MakeRecollectedGuides(WindowCenter);
	const TArray<FTDFluidWaterRegion> Regions = MakeRasterRegions(WindowCenter);

	TestEqual(TEXT("S2 guide update carries only cell, surface, target, weight and descent (no seed field)"), static_cast<int32>(sizeof(FTDFluidGpuWaterGuideCellUpdate)), static_cast<int32>(sizeof(FIntPoint) + sizeof(float) * 5));

	for (const FVector2D& ExtentCm : ExtentCases)
	{
		const FString Label = FString::Printf(TEXT("S2 extent %.0fx%.0f"), ExtentCm.X, ExtentCm.Y);
		FTDFluidSimulationConfig Config = MakeContractWaterConfig(Resolution);
		Config.SimExtentCm = ExtentCm;

		FTDFluidGrid2DSolver Solver;
		Solver.Initialize(Config, WindowCenter);
		Solver.SetFlowGuides(Guides);
		Solver.SetWaterRegions(Regions);
		Solver.StepSimulation(ContractStepSeconds);

		FTDFluidGpuWaterFieldState FieldState;
		FieldState.Initialize(Config, Resolution, 1, Solver.GetCenterWorld(), Solver.GetExtentCm());
		FieldState.SetFlowGuidesAndRegions(Guides, Regions);
		if (!TestEqual(*(Label + TEXT(" field resolution equals the simulation resolution at d=1")), static_cast<int32>(FieldState.GetFieldResolution()), static_cast<int32>(Resolution)))
		{
			return false;
		}

		bool bAllMatch = ExpectFieldMatchesSolver(*this, Label + TEXT(" initial"), FieldState, Solver, Guides, Regions);
		for (const FIntPoint& Shift : Shifts)
		{
			const FString ShiftLabel = FString::Printf(TEXT("%s shift (%d, %d)"), *Label, Shift.X, Shift.Y);
			if (!TestTrue(*(ShiftLabel + TEXT(" moved the solver window")), ShiftSolverWindowExactly(Solver, Shift)))
			{
				return false;
			}

			Solver.StepSimulation(ContractStepSeconds);
			FieldState.ShiftWindow(Shift, Solver.GetCenterWorld());
			FTDFluidGpuActorInput ShiftUpdates;
			FieldState.MoveUpdatesInto(ShiftUpdates);
			const int32 ExpectedNewCells = CountNewShiftCells(Resolution, Shift);
			TestEqual(*(ShiftLabel + TEXT(" resets exactly the new strip terrain cells")), static_cast<int32>(ShiftUpdates.WaterTerrainUpdates.Num()), static_cast<int32>(ExpectedNewCells));
			TestEqual(*(ShiftLabel + TEXT(" every strip reset is unknown terrain")), static_cast<int32>(CountUnknownTerrainUpdates(ShiftUpdates)), static_cast<int32>(ExpectedNewCells));
			bAllMatch &= ExpectFieldMatchesSolver(*this, ShiftLabel, FieldState, Solver, Guides, Regions);
		}

		Solver.SetFlowGuides(RecollectedGuides);
		Solver.StepSimulation(ContractStepSeconds);
		FieldState.SetFlowGuidesAndRegions(RecollectedGuides, Regions);
		bAllMatch &= ExpectFieldMatchesSolver(*this, Label + TEXT(" recollected"), FieldState, Solver, RecollectedGuides, Regions);

		const FIntPoint TerrainCell(3, 4);
		constexpr float TerrainElevationCm = -87.5f;
		FieldState.SetTerrainElevationAtFieldCell(TerrainCell.X, TerrainCell.Y, TerrainElevationCm);
		FTDFluidGpuActorInput TerrainUpdates;
		FieldState.MoveUpdatesInto(TerrainUpdates);
		float SentElevationCm = 0.0f;
		TestEqual(*(Label + TEXT(" a traced terrain cell is sent on the next gather")), static_cast<int32>(TerrainUpdates.WaterTerrainUpdates.Num()), 1);
		TestTrue(*(Label + TEXT(" the traced terrain value is sent immediately")), FindTerrainUpdate(TerrainUpdates, TerrainCell, SentElevationCm) && AreFloatBitsEqual(SentElevationCm, TerrainElevationCm));

		FieldState.RequestFullUpload(ExpectedFullUploadGatherCount);
		for (int32 GatherIndex = 1; GatherIndex <= ExpectedFullUploadGatherCount + 1; ++GatherIndex)
		{
			FTDFluidGpuActorInput GatherUpdates;
			FieldState.MoveUpdatesInto(GatherUpdates);
			const bool bExpectsFullUpload = GatherIndex <= ExpectedFullUploadGatherCount;
			TestEqual(*FString::Printf(TEXT("%s gather %d after RequestFullUpload(3) is %s"), *Label, GatherIndex, bExpectsFullUpload ? TEXT("full") : TEXT("partial")), IsFullWaterUpload(GatherUpdates, Resolution), bExpectsFullUpload);
		}

		if (!bAllMatch)
		{
			ReportLine(*this, FString::Printf(TEXT("%s: at least one raster comparison failed"), *Label));
		}
	}

	{
		const FString Label(TEXT("S2 divisor 4"));
		FTDFluidSimulationConfig Config = MakeContractWaterConfig(Resolution);
		FTDFluidGpuWaterFieldState QuarterFieldState;
		QuarterFieldState.Initialize(Config, Resolution, 4, WindowCenter, Config.SimExtentCm);
		QuarterFieldState.SetFlowGuidesAndRegions(Guides, Regions);
		const int32 FieldResolution = QuarterFieldState.GetFieldResolution();
		if (!TestEqual(*(Label + TEXT(" field resolution is a quarter")), static_cast<int32>(FieldResolution), static_cast<int32>(Resolution / 4)))
		{
			return false;
		}

		const FTDFluidGpuActorInput FullUpload = TakeFullFieldUpload(QuarterFieldState);
		TArray<double> RowWorldY;
		RowWorldY.SetNumUninitialized(FieldResolution);
		for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
		{
			RowWorldY[FieldY] = QuarterFieldState.FieldCellToPlaneCenter(0, FieldY).Y;
		}
		const FTDFluidWaterInputRaster IndependentRaster = MakeIndependentRaster(Guides, Regions, RowWorldY, 0.5 * Config.SimExtentCm.Y / FieldResolution + Config.FlowGuideBankFalloffCm);
		const FTDWaterContractDenormalScope DenormalScope;
		int32 MismatchCount = 0;
		int32 GuidedCount = 0;
		for (const FTDFluidGpuWaterGuideCellUpdate& Update : FullUpload.WaterGuideUpdates)
		{
			const FTDFluidWaterRasterCell Expected = IndependentRaster.RasterizeCell(QuarterFieldState.FieldCellToPlaneCenter(Update.FieldCell.X, Update.FieldCell.Y), Update.FieldCell.Y, Config.FlowGuideBankFalloffCm, Config.FlowGuideBankSpeedRatio, FTDFluidGrid2DSolver::NoWaterSurfaceHeight);
			const float ExpectedUploadSurfaceCm = Expected.SurfaceHeightCm == FTDFluidGrid2DSolver::NoWaterSurfaceHeight ? TDFluidGpuSolverConstants::WaterNoSurfaceHeightCm : Expected.SurfaceHeightCm;
			GuidedCount += Expected.GuideWeight > 0.0f ? 1 : 0;
			const bool bMatches = AreFloatBitsEqual(Update.SurfaceHeightCm, ExpectedUploadSurfaceCm)
				&& AreFloatBitsEqual(Update.TargetVelocityCmPerSec.X, Expected.TargetVelocityX)
				&& AreFloatBitsEqual(Update.TargetVelocityCmPerSec.Y, Expected.TargetVelocityY)
				&& AreFloatBitsEqual(Update.GuideWeight, Expected.GuideWeight)
				&& AreFloatBitsEqual(Update.GuideDescent, Expected.GuideDescent);
			MismatchCount += bMatches ? 0 : 1;
		}
		TestEqual(*(Label + TEXT(" full upload carries every field cell")), static_cast<int32>(FullUpload.WaterGuideUpdates.Num()), static_cast<int32>(FieldResolution * FieldResolution));
		TestTrue(*(Label + TEXT(" some field cells are guided")), GuidedCount > 0);
		TestEqual(*(Label + TEXT(" field cell values equal RasterizeCell at the field cell centre")), static_cast<int32>(MismatchCount), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterContractProxyLifecycleKeepsTerrainAndGuidesTest,
	"TDGame.FluidFX.Water.Gpu.ProxyLifecycleKeepsTerrainAndGuides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterContractProxyLifecycleKeepsTerrainAndGuidesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterContractTestPrivate;

	const FString Label(TEXT("S12 ProxyLifecycle"));
	FTDWaterGpuConsoleVariableScope VelocityFormatScope(VelocityFloat32VariableName, -1);
	if (!VelocityFormatScope.IsValid())
	{
		AddError(FString::Printf(TEXT("%s: %s is not registered, so the water batch key cannot be changed from the console"), *Label, VelocityFloat32VariableName));
		return false;
	}

	constexpr int32 Resolution = 192;
	const FVector2D WindowCenter(310.0, -145.0);
	FTDFluidSimulationConfig Config = MakeContractWaterConfig(Resolution);
	Config.SolverBackend = ETDFluidSolverBackend::Gpu;
	FTDFluidGpuSolverProxy Proxy;
	Proxy.Initialize(Config, Resolution, WindowCenter);
	TestEqual(*(Label + TEXT(" (1) water 192 keeps the exact tier")), static_cast<int32>(Proxy.GetResolution()), static_cast<int32>(Resolution));
	TestTrue(*(Label + TEXT(" (1) proxy uses the water layer")), Proxy.UsesWaterLayer());
	const int32 FieldResolution = Proxy.GetWaterFieldResolution();
	if (!TestEqual(*(Label + TEXT(" (1) field resolution is the full tier")), static_cast<int32>(FieldResolution), static_cast<int32>(Resolution)))
	{
		return false;
	}

	Proxy.SetWaterInputs(MakeRasterGuides(WindowCenter), MakeRasterRegions(WindowCenter));
	Proxy.AssignActorKey(1);
	uint32 FrameNumber = 0;

	for (int32 GatherIndex = 1; GatherIndex <= ExpectedFullUploadGatherCount + 1; ++GatherIndex)
	{
		const FTDFluidGpuActorInput ActorInput = GatherProxyStep(Proxy, ++FrameNumber, ContractStepSeconds);
		const bool bExpectsFullUpload = GatherIndex <= ExpectedFullUploadGatherCount;
		TestEqual(*FString::Printf(TEXT("%s (5) initial gather %d is %s"), *Label, GatherIndex, bExpectsFullUpload ? TEXT("full") : TEXT("partial")), IsFullWaterUpload(ActorInput, FieldResolution), bExpectsFullUpload);
	}

	const int32 InitialGeneration = Proxy.GetGeneration();
	for (int32 CellY = 0; CellY < FieldResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < FieldResolution; ++CellX)
		{
			Proxy.SetTerrainElevationAtCell(CellX, CellY, FlatBedElevationCm - 0.25f * CellX);
		}
	}
	const FTDFluidGpuActorInput TerrainGather = GatherProxyStep(Proxy, ++FrameNumber, ContractStepSeconds);
	TestEqual(*(Label + TEXT(" (2) every traced terrain cell is sent")), static_cast<int32>(TerrainGather.WaterTerrainUpdates.Num()), static_cast<int32>(FieldResolution * FieldResolution));

	FTDFluidSimulationConfig BankConfig = Config;
	BankConfig.FlowGuideBankFalloffCm = 90.0f;
	Proxy.UpdateSettings(BankConfig);
	TestEqual(*(Label + TEXT(" (2) bank change keeps the generation")), static_cast<int32>(Proxy.GetGeneration()), static_cast<int32>(InitialGeneration));
	const FTDFluidGpuActorInput BankGather = GatherProxyStep(Proxy, ++FrameNumber, ContractStepSeconds);
	TestEqual(*(Label + TEXT(" (2) bank change sends no unknown terrain")), static_cast<int32>(CountUnknownTerrainUpdates(BankGather)), 0);
	TestEqual(*(Label + TEXT(" (2) bank change re-rasterizes every guide cell")), static_cast<int32>(BankGather.WaterGuideUpdates.Num()), static_cast<int32>(FieldResolution * FieldResolution));
	TestEqual(*(Label + TEXT(" (2) bank change is not a full upload")), static_cast<int32>(BankGather.WaterTerrainUpdates.Num()), 0);

	Proxy.ResetState();
	TestEqual(*(Label + TEXT(" (3) reset advances the generation")), static_cast<int32>(Proxy.GetGeneration()), static_cast<int32>(InitialGeneration + 1));
	const FTDFluidGpuActorInput ResetGather = GatherProxyStep(Proxy, ++FrameNumber, ContractStepSeconds);
	TestTrue(*(Label + TEXT(" (3) reset gather is a full upload")), IsFullWaterUpload(ResetGather, FieldResolution));
	TestEqual(*(Label + TEXT(" (3) reset forgets every terrain cell")), static_cast<int32>(CountUnknownTerrainUpdates(ResetGather)), static_cast<int32>(FieldResolution * FieldResolution));
	TestTrue(*(Label + TEXT(" (3) guide segments survive the reset without SetWaterInputs")), CountGuidedUpdates(ResetGather) > 0);
	for (int32 GatherIndex = 2; GatherIndex <= ExpectedFullUploadGatherCount; ++GatherIndex)
	{
		GatherProxyStep(Proxy, ++FrameNumber, ContractStepSeconds);
	}

	const FIntPoint KeptCell(5, 6);
	constexpr float KeptElevationCm = -55.0f;
	Proxy.SetTerrainElevationAtCell(KeptCell.X, KeptCell.Y, KeptElevationCm);
	const FTDFluidGpuActorInput ZeroStepGather = GatherProxyStep(Proxy, ++FrameNumber, 0.0f);
	TestEqual(*(Label + TEXT(" (4) a zero step moves no terrain update")), static_cast<int32>(ZeroStepGather.WaterTerrainUpdates.Num()), 0);
	TestEqual(*(Label + TEXT(" (4) a zero step moves no guide update")), static_cast<int32>(ZeroStepGather.WaterGuideUpdates.Num()), 0);
	const FTDFluidGpuActorInput FiniteStepGather = GatherProxyStep(Proxy, ++FrameNumber, ContractStepSeconds);
	float KeptSentElevationCm = 0.0f;
	TestTrue(*(Label + TEXT(" (4) the next finite step carries the held terrain update")), FindTerrainUpdate(FiniteStepGather, KeptCell, KeptSentElevationCm) && AreFloatBitsEqual(KeptSentElevationCm, KeptElevationCm));

	const int32 GenerationBeforeKeyChange = Proxy.GetGeneration();
	VelocityFormatScope.SetValue(0);
	for (int32 GatherIndex = 1; GatherIndex <= ExpectedFullUploadGatherCount + 1; ++GatherIndex)
	{
		const FTDFluidGpuActorInput ActorInput = GatherProxyStep(Proxy, ++FrameNumber, ContractStepSeconds);
		const bool bExpectsFullUpload = GatherIndex <= ExpectedFullUploadGatherCount;
		TestEqual(*FString::Printf(TEXT("%s (5) gather %d after the format change is %s"), *Label, GatherIndex, bExpectsFullUpload ? TEXT("full") : TEXT("partial")), IsFullWaterUpload(ActorInput, FieldResolution), bExpectsFullUpload);
		if (GatherIndex != 1)
		{
			continue;
		}

		float KeyChangeElevationCm = 0.0f;
		TestEqual(*(Label + TEXT(" (7) format key change gather carries every guide cell")), static_cast<int32>(ActorInput.WaterGuideUpdates.Num()), static_cast<int32>(FieldResolution * FieldResolution));
		TestEqual(*(Label + TEXT(" (7) format key change gather carries every terrain cell")), static_cast<int32>(ActorInput.WaterTerrainUpdates.Num()), static_cast<int32>(FieldResolution * FieldResolution));
		TestTrue(*(Label + TEXT(" (7) format key change keeps the confirmed terrain value")), FindTerrainUpdate(ActorInput, KeptCell, KeyChangeElevationCm) && AreFloatBitsEqual(KeyChangeElevationCm, KeptElevationCm));
	}
	TestEqual(*(Label + TEXT(" (7) format key change keeps the generation")), static_cast<int32>(Proxy.GetGeneration()), static_cast<int32>(GenerationBeforeKeyChange));
	VelocityFormatScope.SetValue(-1);
	for (int32 GatherIndex = 1; GatherIndex <= ExpectedFullUploadGatherCount; ++GatherIndex)
	{
		GatherProxyStep(Proxy, ++FrameNumber, ContractStepSeconds);
	}

	FTDFluidSimulationConfig IterationConfig = BankConfig;
	IterationConfig.PressureIterations = 8;
	Proxy.UpdateSettings(IterationConfig);
	const FTDFluidGpuActorInput IterationGather = GatherProxyStep(Proxy, ++FrameNumber, ContractStepSeconds);
	float IterationElevationCm = 0.0f;
	TestEqual(*(Label + TEXT(" (7) iteration key change gather carries every guide cell")), static_cast<int32>(IterationGather.WaterGuideUpdates.Num()), static_cast<int32>(FieldResolution * FieldResolution));
	TestEqual(*(Label + TEXT(" (7) iteration key change gather carries every terrain cell")), static_cast<int32>(IterationGather.WaterTerrainUpdates.Num()), static_cast<int32>(FieldResolution * FieldResolution));
	TestTrue(*(Label + TEXT(" (7) iteration key change keeps the confirmed terrain value")), FindTerrainUpdate(IterationGather, KeptCell, IterationElevationCm) && AreFloatBitsEqual(IterationElevationCm, KeptElevationCm));
	TestEqual(*(Label + TEXT(" (7) iteration key change keeps the generation")), static_cast<int32>(Proxy.GetGeneration()), static_cast<int32>(GenerationBeforeKeyChange));
	for (int32 GatherIndex = 2; GatherIndex <= ExpectedFullUploadGatherCount; ++GatherIndex)
	{
		GatherProxyStep(Proxy, ++FrameNumber, ContractStepSeconds);
	}

	FTDFluidSimulationConfig DryConfig = IterationConfig;
	DryConfig.bEnableWaterLayer = false;
	Proxy.UpdateSettings(DryConfig);
	TestFalse(*(Label + TEXT(" (6) water off releases the water layer")), Proxy.UsesWaterLayer());
	TestEqual(*(Label + TEXT(" (6) water off releases the field")), static_cast<int32>(Proxy.GetWaterFieldResolution()), 0);
	Proxy.UpdateSettings(IterationConfig);
	TestTrue(*(Label + TEXT(" (6) water on restores the water layer")), Proxy.UsesWaterLayer());
	const FTDFluidGpuActorInput ToggleGather = GatherProxyStep(Proxy, ++FrameNumber, ContractStepSeconds);
	TestTrue(*(Label + TEXT(" (6) water on sends a full upload")), IsFullWaterUpload(ToggleGather, FieldResolution));
	TestEqual(*(Label + TEXT(" (6) water on starts with unknown terrain")), static_cast<int32>(CountUnknownTerrainUpdates(ToggleGather)), static_cast<int32>(FieldResolution * FieldResolution));

	FTDFluidSimulationConfig ManyStampConfig = IterationConfig;
	ManyStampConfig.MaxPointsPerTick = 1280;
	Proxy.UpdateSettings(ManyStampConfig);
	TestEqual(*(Label + TEXT(" (8) water stamp cap follows the CPU limit 512")), static_cast<int32>(GatherProxyStep(Proxy, ++FrameNumber, ContractStepSeconds).Settings.MaxStampsPerTick), 512);
	ManyStampConfig.MaxPointsPerTick = 300;
	Proxy.UpdateSettings(ManyStampConfig);
	TestEqual(*(Label + TEXT(" (8) water stamp cap keeps 300")), static_cast<int32>(GatherProxyStep(Proxy, ++FrameNumber, ContractStepSeconds).Settings.MaxStampsPerTick), 300);

	FTDFluidSimulationConfig PainterConfig = Config;
	PainterConfig.SimMode = ETDFluidSimMode::SimplePainter;
	FTDFluidGpuSolverProxy PainterProxy;
	PainterProxy.Initialize(PainterConfig, Resolution, WindowCenter);
	TestEqual(*(Label + TEXT(" (8) painter with the water flag uses the power of two tier")), static_cast<int32>(PainterProxy.GetResolution()), 256);
	TestFalse(*(Label + TEXT(" (8) painter with the water flag is not a water layer")), PainterProxy.UsesWaterLayer());

	FTDWaterGpuConsoleVariableScope BackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	if (!WaterGpuScope.IsValid())
	{
		AddError(MakeWaterGpuMissingMessage(Label));
		return false;
	}

	FTDFluidGpuBackendEnvironment Environment = FTDFluidGpuBackendSelection::CaptureEnvironment();
	Environment.bHasRenderingRhi = true;
	Environment.bSupportsShaderModel5 = true;
	FTDFluidGpuBackendQuery PainterQuery;
	PainterQuery.Preference = ETDFluidGpuSolverPreference::Gpu;
	PainterQuery.bIsWaterLayerActive = true;
	PainterQuery.RequestedResolution = Resolution;
	PainterQuery.bIsSimplePainter = true;
	const FTDFluidGpuBackendDecision PainterDecision = FTDFluidGpuBackendSelection::Decide(PainterQuery, Environment);
	TestTrue(*(Label + TEXT(" (8) painter decision uses the GPU")), PainterDecision.bUsesGpu);
	TestEqual(*(Label + TEXT(" (8) painter decision matches the proxy tier")), static_cast<int32>(PainterDecision.Resolution), static_cast<int32>(PainterProxy.GetResolution()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterContractProbeSamplesMatchOutputsTest,
	"TDGame.FluidFXRhi.Water.Probe.SamplesMatchOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterContractProbeSamplesMatchOutputsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterContractTestPrivate;

	const FString Label(TEXT("S9 Probe.SamplesMatchOutputs"));
	if (!CanRunRhiContractTest(*this, Label) || !RequireWaterGpuVariable(*this, Label))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope BackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	FTDFluidGpuWaterContractTestWorld TestWorld;
	UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.GetFluidSubsystem();
	if (!TestNotNull(*(Label + TEXT(" has a fluid subsystem")), FluidSubsystem))
	{
		return false;
	}

	FTDWaterRiverScene Scene;
	if (!SpawnRiverWaterScene(*this, Label, *TestWorld.World, Scene))
	{
		return false;
	}

	UTDFluidLiveComponent& Component = *Scene.WaterActor->LiveComponent;
	if (!RequireGpuWaterLayer(*this, Label, Component) || !WaitForTerrainCompletion(*this, Label, TestWorld, Component))
	{
		return false;
	}

	TestEqual(*(Label + TEXT(" runs the exact 192 tier")), static_cast<int32>(Component.GetSimulationResolution()), static_cast<int32>(RiverSceneResolution));
	TMap<uint32, FTDWaterOutputFrame> Frames;
	const TArray<FVector> Points = MakeRiverProbePoints();
	TestEqual(*(Label + TEXT(" registers 64 points")), static_cast<int32>(Points.Num()), static_cast<int32>(TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame));
	RunProbeComparisonPhase(*this, Label + TEXT(" 64 points"), TestWorld, Component, *FluidSubsystem, Points, Frames);

	TArray<FTDFluidWaterPointSample> Samples;
	TArray<FVector> ManyPoints(Points);
	ManyPoints.Add(FVector(-1200.0, 600.0, 0.0));
	Component.SetWaterProbeLocations(ManyPoints);
	bool bHasManySamples = false;
	for (int32 FrameIndex = 0; FrameIndex < ProbeGenerationFrameLimit && !bHasManySamples; ++FrameIndex)
	{
		StepAndCaptureFrame(TestWorld, Component, *FluidSubsystem, Frames);
		bHasManySamples = Component.GetWaterProbeSamples(Samples);
	}
	TestTrue(*(Label + TEXT(" 65 points return samples within 3 frames")), bHasManySamples);
	TestEqual(*(Label + TEXT(" 65 points keep only 64 samples")), static_cast<int32>(Samples.Num()), static_cast<int32>(TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame));

	TArray<FVector> ReplacedPoints;
	for (int32 PointIndex = Points.Num() - 1; PointIndex >= 0; PointIndex -= 2)
	{
		ReplacedPoints.Add(Points[PointIndex] + FVector(11.0, -9.0, 0.0));
	}
	RunProbeComparisonPhase(*this, Label + TEXT(" replaced set"), TestWorld, Component, *FluidSubsystem, ReplacedPoints, Frames);

	TArray<AActor*> ExtraActors;
	for (int32 SmokeIndex = 0; SmokeIndex < 2; ++SmokeIndex)
	{
		FTDFluidSimulationConfig SmokeConfig = UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind::Smoke);
		SmokeConfig.GridResolution = 128;
		SmokeConfig.bEnableWaterLayer = false;
		SmokeConfig.SolverBackend = ETDFluidSolverBackend::Gpu;
		ExtraActors.Add(SpawnContractLiveActor(*TestWorld.World, FVector(-900.0 + 1800.0 * SmokeIndex, 2600.0, 0.0), SmokeConfig));
	}
	FTDFluidSimulationConfig ExtraWaterConfig = MakeContractWaterConfig(64);
	ExtraWaterConfig.SolverBackend = ETDFluidSolverBackend::Gpu;
	ATDFluidLiveActor* ExtraWaterActor = SpawnContractLiveActor(*TestWorld.World, FVector(0.0, -2600.0, 0.0), ExtraWaterConfig);
	ExtraActors.Add(ExtraWaterActor);
	for (const AActor* ExtraActor : ExtraActors)
	{
		if (!TestNotNull(*(Label + TEXT(" spawned an extra GPU actor")), ExtraActor))
		{
			return false;
		}
	}
	TArray<FVector> ExtraProbePoints;
	for (int32 PointIndex = 0; PointIndex < 4; ++PointIndex)
	{
		ExtraProbePoints.Add(FVector(-200.0 + 100.0 * PointIndex, -2600.0, 0.0));
	}
	ExtraWaterActor->LiveComponent->SetWaterProbeLocations(ExtraProbePoints);
	RunProbeComparisonPhase(*this, Label + TEXT(" with 4 GPU actors"), TestWorld, Component, *FluidSubsystem, Points.FilterByPredicate([](const FVector& Point) { return Point.Y >= 0.0; }), Frames);

	for (AActor* ExtraActor : ExtraActors)
	{
		ExtraActor->Destroy();
	}
	RunProbeComparisonPhase(*this, Label + TEXT(" back to 1 GPU actor"), TestWorld, Component, *FluidSubsystem, Points, Frames);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterContractRippleStepAdvectionOvershootParityTest,
	"TDGame.FluidFXRhi.Water.Ripple.StepAdvectionOvershootParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterContractRippleStepAdvectionOvershootParityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterContractTestPrivate;

	const FString Label(TEXT("S11 Ripple.StepAdvectionOvershootParity"));
	if (!CanRunRhiContractTest(*this, Label) || !RequireWaterGpuVariable(*this, Label))
	{
		return false;
	}

	constexpr int32 Resolution = 64;
	constexpr int32 StepCornerCellX = 32;
	constexpr int32 StepCornerCellY = 24;
	constexpr float StepHeightCm = 1.0f;
	constexpr float FlowSpeedCmPerSec = 250.0f;
	constexpr float GuideHalfWidthCm = 5000.0f;
	constexpr double GuideHalfLengthCm = 1500.0;
	constexpr int32 StepsPerFlowDirection = 60;
	constexpr int32 FlowCycleCount = 3;
	constexpr float ExtremumToleranceCm = 0.0001f;
	constexpr float ReadoutToleranceCm = 0.001f;
	constexpr float ClampMarginRatio = 0.25f;
	constexpr float RippleHeightReferenceCm = 1.5f;
	constexpr float ReadoutUnitCm = RippleHeightReferenceCm / 1024.0f;
	constexpr float PlateauToleranceCm = ReadoutUnitCm + ExtremumToleranceCm;
	constexpr int32 ClampBandFirstCycleIndex = 1;
	constexpr float CycleOvershootGuardCm = 0.05f;
	constexpr int32 StampsPerFrameLimit = 512;
	const FVector2D FlowDirections[] = { FVector2D(1.0, 1.0), FVector2D(-1.0, 1.0), FVector2D(-1.0, -1.0), FVector2D(1.0, -1.0) };

	FTDWaterGpuConsoleVariableScope BackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	FTDFluidGpuWaterContractTestWorld TestWorld;

	FTDFluidSimulationConfig CpuConfig = MakeContractWaterConfig(Resolution);
	CpuConfig.BorderFadeRatio = 0.0f;
	CpuConfig.VorticityStrength = 0.0f;
	CpuConfig.RippleWaveSpeedCmPerSec = 0.001f;
	CpuConfig.RippleDampingPerSec = 0.0f;
	CpuConfig.RippleFlowAdvection = 1.0f;
	CpuConfig.RippleHeightReferenceCm = RippleHeightReferenceCm;
	CpuConfig.SolverBackend = ETDFluidSolverBackend::Cpu;
	FTDFluidSimulationConfig GpuConfig = CpuConfig;
	GpuConfig.SolverBackend = ETDFluidSolverBackend::Gpu;

	ATDFluidLiveActor* CpuActor = SpawnContractLiveActor(*TestWorld.World, FVector::ZeroVector, CpuConfig);
	ATDFluidLiveActor* GpuActor = SpawnContractLiveActor(*TestWorld.World, FVector::ZeroVector, GpuConfig);
	if (!TestNotNull(*(Label + TEXT(" spawned the CPU arm")), CpuActor) || !TestNotNull(*(Label + TEXT(" spawned the GPU arm")), GpuActor))
	{
		return false;
	}

	UTDFluidLiveComponent& CpuComponent = *CpuActor->LiveComponent;
	UTDFluidLiveComponent& GpuComponent = *GpuActor->LiveComponent;
	if (!TestFalse(*(Label + TEXT(" CPU arm uses the CPU solver")), CpuComponent.IsUsingGpuSolver()) || !RequireGpuWaterLayer(*this, Label, GpuComponent))
	{
		return false;
	}
	TestFalse(*(Label + TEXT(" terrain sampling stays off so terrain stays unknown")), CpuActor->bSampleTerrainElevation || GpuActor->bSampleTerrainElevation);

	TArray<FTDFluidBrushStamp> Stamps;
	for (int32 CellY = StepCornerCellY; CellY < Resolution; ++CellY)
	{
		for (int32 CellX = StepCornerCellX; CellX < Resolution; ++CellX)
		{
			const FVector2D CellCenter = CpuComponent.CellToPlaneCenter(CellX, CellY);
			FTDFluidBrushStamp& Stamp = Stamps.AddDefaulted_GetRef();
			Stamp.WorldLocation = FVector(CellCenter.X, CellCenter.Y, 0.0);
			Stamp.RadiusCm = 5.0f;
			Stamp.DensityStrength = 0.0f;
			Stamp.VelocityStrength = 0.0f;
			Stamp.RippleImpulseCm = -StepHeightCm;
			Stamp.Channel = ETDFluidBrushChannel::ChannelA_Water;
		}
	}

	for (int32 FirstStamp = 0; FirstStamp < Stamps.Num(); FirstStamp += StampsPerFrameLimit)
	{
		const int32 EndStamp = FMath::Min(FirstStamp + StampsPerFrameLimit, Stamps.Num());
		for (int32 StampIndex = FirstStamp; StampIndex < EndStamp; ++StampIndex)
		{
			CpuComponent.QueueBrushStamp(Stamps[StampIndex]);
			GpuComponent.QueueBrushStamp(Stamps[StampIndex]);
		}
		TestWorld.StepWorld(ContractStepSeconds, true);
	}

	struct FTDRippleArmTrace
	{
		const TCHAR* ArmName = TEXT("");
		FFloatInterval InitialRangeCm;
		FFloatInterval VisitedRangeCm;
		FFloatInterval CycleRangeCm;
		FFloatInterval LaterCyclesRangeCm;
		TArray<float> CycleOvershootCm;
		FVector2D FirstFlowVelocity = FVector2D::ZeroVector;
		int32 NonFiniteCount = 0;
		bool bHasReadAllFrames = true;
	};

	FTDRippleArmTrace CpuTrace;
	CpuTrace.ArmName = TEXT("CPU");
	FTDRippleArmTrace GpuTrace;
	GpuTrace.ArmName = TEXT("GPU");
	TArray<FFloat16Color> GpuSurfacePixels;
	TArray<FFloat16Color> GpuVelocityPixels;
	const auto ReadArmPixels = [&](bool bIsGpuArm, const TArray<FFloat16Color>*& OutSurfacePixels, const TArray<FFloat16Color>*& OutVelocityPixels)
	{
		if (!bIsGpuArm)
		{
			OutSurfacePixels = &CpuComponent.GetPackedWaterSurfacePixels();
			OutVelocityPixels = &CpuComponent.GetPackedVelocityPixels();
			return OutSurfacePixels->Num() == Resolution * Resolution && OutVelocityPixels->Num() == Resolution * Resolution;
		}

		const bool bHasRead = ReadRenderTargetPixels(GpuComponent.GetWaterSurfaceOutputTexture(), GpuSurfacePixels) && ReadRenderTargetPixels(GpuComponent.GetVelocityTexture(), GpuVelocityPixels);
		OutSurfacePixels = &GpuSurfacePixels;
		OutVelocityPixels = &GpuVelocityPixels;
		return bHasRead && GpuSurfacePixels.Num() == Resolution * Resolution && GpuVelocityPixels.Num() == Resolution * Resolution;
	};
	const auto IncludeRippleHeights = [](const TArray<FFloat16Color>& SurfacePixels, FFloatInterval& RangeCm, int32& InOutNonFiniteCount)
	{
		for (const FFloat16Color& Pixel : SurfacePixels)
		{
			const float HeightCm = DecodeRipplePixel(Pixel, RippleHeightReferenceCm);
			if (!FMath::IsFinite(HeightCm))
			{
				++InOutNonFiniteCount;
				continue;
			}
			RangeCm.Include(HeightCm);
		}
	};

	for (FTDRippleArmTrace* Trace : { &CpuTrace, &GpuTrace })
	{
		const TArray<FFloat16Color>* SurfacePixels = nullptr;
		const TArray<FFloat16Color>* VelocityPixels = nullptr;
		if (!TestTrue(*FString::Printf(TEXT("%s %s arm outputs are readable after the stamps"), *Label, Trace->ArmName), ReadArmPixels(Trace == &GpuTrace, SurfacePixels, VelocityPixels)))
		{
			return false;
		}
		IncludeRippleHeights(*SurfacePixels, Trace->InitialRangeCm, Trace->NonFiniteCount);
		Trace->VisitedRangeCm = Trace->InitialRangeCm;
	}

	const float GpuInitialAmplitudeCm = FMath::Max(FMath::Abs(GpuTrace.InitialRangeCm.Min), FMath::Abs(GpuTrace.InitialRangeCm.Max));
	if (GpuInitialAmplitudeCm < 0.5f * StepHeightCm)
	{
		AddError(MakeNoWaterActivityMessage(Label, TEXT("ripple")));
		return false;
	}

	int32 SimulatedStepCount = 0;
	for (int32 Cycle = 0; Cycle < FlowCycleCount; ++Cycle)
	{
		CpuTrace.CycleRangeCm = CpuTrace.InitialRangeCm;
		GpuTrace.CycleRangeCm = GpuTrace.InitialRangeCm;
		for (const FVector2D& FlowDirection : FlowDirections)
		{
			TArray<FTDFluidFlowGuide> Guides;
			Guides.Add(MakeStraightGuide(-FlowDirection * GuideHalfLengthCm, FlowDirection * GuideHalfLengthCm, GuideHalfWidthCm, FlowSpeedCmPerSec, 0.0f));
			CpuComponent.SetWaterGuidesAndRegions(Guides, TArray<FTDFluidWaterRegion>());
			GpuComponent.SetWaterGuidesAndRegions(Guides, TArray<FTDFluidWaterRegion>());
			for (int32 Step = 0; Step < StepsPerFlowDirection; ++Step)
			{
				TestWorld.StepWorld(ContractStepSeconds, true);
				++SimulatedStepCount;
				for (FTDRippleArmTrace* Trace : { &CpuTrace, &GpuTrace })
				{
					const TArray<FFloat16Color>* SurfacePixels = nullptr;
					const TArray<FFloat16Color>* VelocityPixels = nullptr;
					if (!ReadArmPixels(Trace == &GpuTrace, SurfacePixels, VelocityPixels))
					{
						Trace->bHasReadAllFrames = false;
						continue;
					}

					IncludeRippleHeights(*SurfacePixels, Trace->VisitedRangeCm, Trace->NonFiniteCount);
					IncludeRippleHeights(*SurfacePixels, Trace->CycleRangeCm, Trace->NonFiniteCount);
					if (SimulatedStepCount == StepsPerFlowDirection)
					{
						Trace->FirstFlowVelocity = DecodeVelocityPixel((*VelocityPixels)[(Resolution / 2) * Resolution + Resolution / 2]);
					}
				}
			}
		}

		for (FTDRippleArmTrace* Trace : { &CpuTrace, &GpuTrace })
		{
			Trace->CycleOvershootCm.Add(FMath::Max(Trace->CycleRangeCm.Max - Trace->InitialRangeCm.Max, Trace->InitialRangeCm.Min - Trace->CycleRangeCm.Min));
			if (Cycle >= ClampBandFirstCycleIndex)
			{
				Trace->LaterCyclesRangeCm.Include(Trace->CycleRangeCm.Min);
				Trace->LaterCyclesRangeCm.Include(Trace->CycleRangeCm.Max);
			}
		}
	}

	for (FTDRippleArmTrace* Trace : { &CpuTrace, &GpuTrace })
	{
		const FString ArmLabel = FString::Printf(TEXT("%s %s arm"), *Label, Trace->ArmName);
		const TArray<FFloat16Color>* SurfacePixels = nullptr;
		const TArray<FFloat16Color>* VelocityPixels = nullptr;
		FFloatInterval FinalRangeCm;
		int32 FinalNonFiniteCount = 0;
		if (ReadArmPixels(Trace == &GpuTrace, SurfacePixels, VelocityPixels))
		{
			IncludeRippleHeights(*SurfacePixels, FinalRangeCm, FinalNonFiniteCount);
		}

		ReportLine(*this, FString::Printf(TEXT("%s: steps=%d initial=[%.6f, %.6f] visited=[%.6f, %.6f] final=[%.6f, %.6f] cycle_overshoot=[%s] first_flow=(%.1f, %.1f)"),
			*ArmLabel, SimulatedStepCount, Trace->InitialRangeCm.Min, Trace->InitialRangeCm.Max, Trace->VisitedRangeCm.Min, Trace->VisitedRangeCm.Max, FinalRangeCm.Min, FinalRangeCm.Max,
			*FString::JoinBy(Trace->CycleOvershootCm, TEXT(", "), [](float Value) { return FString::Printf(TEXT("%.6f"), Value); }), Trace->FirstFlowVelocity.X, Trace->FirstFlowVelocity.Y));
		TestTrue(*(ArmLabel + TEXT(" read every frame")), Trace->bHasReadAllFrames);
		TestTrue(*(ArmLabel + TEXT(" step starts at zero")), FMath::Abs(Trace->InitialRangeCm.Min) <= ReadoutToleranceCm);
		TestTrue(*(ArmLabel + TEXT(" step plateau starts at the step height")), FMath::Abs(Trace->InitialRangeCm.Max - StepHeightCm) <= PlateauToleranceCm);
		TestTrue(*(ArmLabel + TEXT(" first flow runs diagonally on both axes")), Trace->FirstFlowVelocity.X > 100.0 && Trace->FirstFlowVelocity.Y > 100.0);
		TestEqual(*(ArmLabel + TEXT(" every ripple height stays finite")), static_cast<int32>(Trace->NonFiniteCount + FinalNonFiniteCount), 0);
		TestTrue(*(ArmLabel + TEXT(" step still spans both plateaus")), FinalRangeCm.Max >= 0.99f * StepHeightCm && FinalRangeCm.Min <= 0.01f * StepHeightCm);
		TestEqual(*(ArmLabel + TEXT(" every flow cycle reports its overshoot")), static_cast<int32>(Trace->CycleOvershootCm.Num()), static_cast<int32>(FlowCycleCount));
		for (int32 Cycle = 1; Cycle < Trace->CycleOvershootCm.Num(); ++Cycle)
		{
			TestTrue(*FString::Printf(TEXT("%s cycle %d overshoot %.6f cm does not grow past cycle %d overshoot %.6f cm"), *ArmLabel, Cycle, Trace->CycleOvershootCm[Cycle], Cycle - 1, Trace->CycleOvershootCm[Cycle - 1]),
				Trace->CycleOvershootCm[Cycle] <= Trace->CycleOvershootCm[Cycle - 1] + ExtremumToleranceCm + ReadoutToleranceCm);
		}
	}

	const float ClampMarginCm = ClampMarginRatio * (CpuTrace.InitialRangeCm.Max - CpuTrace.InitialRangeCm.Min);
	const float UpperLimitCm = CpuTrace.InitialRangeCm.Max + ClampMarginCm + ExtremumToleranceCm + ReadoutToleranceCm;
	const float LowerLimitCm = CpuTrace.InitialRangeCm.Min - ClampMarginCm - ExtremumToleranceCm - ReadoutToleranceCm;
	ReportLine(*this, FString::Printf(TEXT("%s: CPU clamp margin band [%.6f, %.6f] applies from cycle %d on (later cycles range [%.6f, %.6f]), the first cycle overshoot is informational"),
		*Label, LowerLimitCm, UpperLimitCm, ClampBandFirstCycleIndex + 1, CpuTrace.LaterCyclesRangeCm.Min, CpuTrace.LaterCyclesRangeCm.Max));
	if (TestTrue(*(Label + TEXT(" CPU arm recorded the later cycles")), CpuTrace.LaterCyclesRangeCm.IsValid()))
	{
		TestTrue(*FString::Printf(TEXT("%s CPU arm no ripple height rises above %.6f cm after the first cycle (highest %.6f cm)"), *Label, UpperLimitCm, CpuTrace.LaterCyclesRangeCm.Max), CpuTrace.LaterCyclesRangeCm.Max <= UpperLimitCm);
		TestTrue(*FString::Printf(TEXT("%s CPU arm no ripple height falls below %.6f cm after the first cycle (lowest %.6f cm)"), *Label, LowerLimitCm, CpuTrace.LaterCyclesRangeCm.Min), CpuTrace.LaterCyclesRangeCm.Min >= LowerLimitCm);
	}

	const float VisitedRangeToleranceCm = ExtremumToleranceCm + ReadoutToleranceCm;
	TestTrue(*FString::Printf(TEXT("%s GPU highest ripple height %.6f cm does not exceed the CPU highest %.6f cm by more than %.6f cm"), *Label, GpuTrace.VisitedRangeCm.Max, CpuTrace.VisitedRangeCm.Max, VisitedRangeToleranceCm), GpuTrace.VisitedRangeCm.Max <= CpuTrace.VisitedRangeCm.Max + VisitedRangeToleranceCm);
	TestTrue(*FString::Printf(TEXT("%s GPU lowest ripple height %.6f cm does not fall below the CPU lowest %.6f cm by more than %.6f cm"), *Label, GpuTrace.VisitedRangeCm.Min, CpuTrace.VisitedRangeCm.Min, VisitedRangeToleranceCm), GpuTrace.VisitedRangeCm.Min >= CpuTrace.VisitedRangeCm.Min - VisitedRangeToleranceCm);

	const int32 ComparedCycleCount = FMath::Min(CpuTrace.CycleOvershootCm.Num(), GpuTrace.CycleOvershootCm.Num());
	for (int32 Cycle = 0; Cycle < ComparedCycleCount; ++Cycle)
	{
		TestTrue(*FString::Printf(TEXT("%s cycle %d GPU overshoot %.6f cm stays within %.2f cm of CPU overshoot %.6f cm"), *Label, Cycle, GpuTrace.CycleOvershootCm[Cycle], CycleOvershootGuardCm, CpuTrace.CycleOvershootCm[Cycle]),
			FMath::Abs(GpuTrace.CycleOvershootCm[Cycle] - CpuTrace.CycleOvershootCm[Cycle]) <= CycleOvershootGuardCm);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuWaterContractUpdateSettingsKeepsTerrainTest,
	"TDGame.FluidFXRhi.Water.WaterLayer.UpdateSettingsKeepsTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuWaterContractUpdateSettingsKeepsTerrainTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuWaterContractTestPrivate;

	const FString Label(TEXT("S13 UpdateSettingsKeepsTerrain"));
	if (!CanRunRhiContractTest(*this, Label) || !RequireWaterGpuVariable(*this, Label))
	{
		return false;
	}

	FTDWaterGpuConsoleVariableScope BackendScope(SolverBackendVariableName, 0);
	FTDWaterGpuConsoleVariableScope WaterGpuScope(WaterGpuVariableName, 1);
	FTDFluidGpuWaterContractTestWorld TestWorld;
	UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.GetFluidSubsystem();
	if (!TestNotNull(*(Label + TEXT(" has a fluid subsystem")), FluidSubsystem))
	{
		return false;
	}

	FTDWaterRiverScene Scene;
	if (!SpawnRiverWaterScene(*this, Label, *TestWorld.World, Scene))
	{
		return false;
	}

	UTDFluidLiveComponent& Component = *Scene.WaterActor->LiveComponent;
	if (!RequireGpuWaterLayer(*this, Label, Component) || !WaitForTerrainCompletion(*this, Label, TestWorld, Component))
	{
		return false;
	}

	for (int32 FrameIndex = 0; FrameIndex < UpdateSettingsSettleFrames; ++FrameIndex)
	{
		TestWorld.StepWorld(ContractStepSeconds, true);
	}

	const TArray<FTDWaterGuideCell> GuideCells = BuildStrongGuideCells(Component, *FluidSubsystem);
	const TArray<int32> DryCells = CollectDryBandCells(Component);
	if (!TestTrue(*(Label + TEXT(" the river has strong guide cells")), !GuideCells.IsEmpty()) || !TestTrue(*(Label + TEXT(" the scene has dry bank cells")), !DryCells.IsEmpty()))
	{
		return false;
	}

	FTDWaterOutputFrame Frame;
	if (!TestTrue(*(Label + TEXT(" outputs are readable")), CaptureGpuWaterOutputs(Component, Frame)))
	{
		return false;
	}

	const FTDWaterFlowActivity InitialActivity = MeasureFlowActivity(GuideCells, Frame);
	ReportLine(*this, FString::Printf(TEXT("%s: before settings change %s dry_open=%d/%d"), *Label, *DescribeFlowActivity(InitialActivity), CountOpenDryCells(DryCells, Frame), DryCells.Num()));
	if (!PassesFlowActivity(InitialActivity))
	{
		AddError(MakeNoWaterActivityMessage(Label, TEXT("flow")));
		return false;
	}
	TestEqual(*(Label + TEXT(" dry bank cells are solid before the change")), static_cast<int32>(CountOpenDryCells(DryCells, Frame)), 0);

	const int32 InitialGeneration = Component.GetSimulationGeneration();
	FTDFluidSimulationConfig BankConfig = Component.SimulationConfig;
	BankConfig.FlowGuideBankFalloffCm = FMath::IsNearlyEqual(BankConfig.FlowGuideBankFalloffCm, 90.0f) ? 120.0f : 90.0f;
	Component.ApplySimulationConfig(BankConfig);
	if (!RequireGpuWaterLayer(*this, Label + TEXT(" (a)"), Component))
	{
		return false;
	}

	for (int32 FrameIndex = 0; FrameIndex < UpdateSettingsSettleFrames; ++FrameIndex)
	{
		TestWorld.StepWorld(ContractStepSeconds, true);
	}
	TestTrue(*(Label + TEXT(" (a) outputs are readable after the bank change")), CaptureGpuWaterOutputs(Component, Frame));
	const FTDWaterFlowActivity BankActivity = MeasureFlowActivity(GuideCells, Frame);
	ReportLine(*this, FString::Printf(TEXT("%s (a): %s dry_open=%d/%d"), *Label, *DescribeFlowActivity(BankActivity), CountOpenDryCells(DryCells, Frame), DryCells.Num()));
	TestEqual(*(Label + TEXT(" (a) dry bank cells stay solid 10 frames after the bank change")), static_cast<int32>(CountOpenDryCells(DryCells, Frame)), 0);
	TestEqual(*(Label + TEXT(" (a) bank change keeps the simulation generation")), static_cast<int32>(Component.GetSimulationGeneration()), static_cast<int32>(InitialGeneration));
	TestEqual(*(Label + TEXT(" (a) bank change keeps the 192 tier")), static_cast<int32>(Component.GetSimulationResolution()), static_cast<int32>(RiverSceneResolution));
	if (!PassesFlowActivity(BankActivity))
	{
		AddError(MakeNoWaterActivityMessage(Label + TEXT(" (a)"), TEXT("flow")));
	}

	FTDFluidSimulationConfig IterationConfig = BankConfig;
	IterationConfig.PressureIterations = 8;
	Component.ApplySimulationConfig(IterationConfig);
	TestWorld.StepWorld(ContractStepSeconds, true);
	TestTrue(*(Label + TEXT(" (b) outputs are readable after the iteration change")), CaptureGpuWaterOutputs(Component, Frame));
	const FTDWaterFlowActivity SeedActivity = MeasureFlowActivity(GuideCells, Frame);
	ReportLine(*this, FString::Printf(TEXT("%s (b) first frame: %s dry_open=%d/%d"), *Label, *DescribeFlowActivity(SeedActivity), CountOpenDryCells(DryCells, Frame), DryCells.Num()));
	TestEqual(*(Label + TEXT(" (b) iteration change keeps the simulation generation")), static_cast<int32>(Component.GetSimulationGeneration()), static_cast<int32>(InitialGeneration));
	TestEqual(*(Label + TEXT(" (b) dry bank cells stay solid on the first frame")), static_cast<int32>(CountOpenDryCells(DryCells, Frame)), 0);
	TestTrue(*FString::Printf(TEXT("%s (b) the reseeded guide cells move at least %.2f of the target (%.2f vs %.2f cm/s)"), *Label, SeededSpeedRatioThreshold, SeedActivity.MeanAlongTargetCmPerSec, SeedActivity.MeanTargetCmPerSec),
		SeedActivity.MeanAlongTargetCmPerSec >= SeededSpeedRatioThreshold * SeedActivity.MeanTargetCmPerSec);

	int32 RecoveryFrame = INDEX_NONE;
	for (int32 FrameIndex = 1; FrameIndex <= FlowRecoveryFrameLimit; ++FrameIndex)
	{
		TestWorld.StepWorld(ContractStepSeconds, true);
		if (CaptureGpuWaterOutputs(Component, Frame) && PassesFlowActivity(MeasureFlowActivity(GuideCells, Frame)))
		{
			RecoveryFrame = FrameIndex;
			break;
		}
	}
	ReportLine(*this, FString::Printf(TEXT("%s (b): flow activity recovered after %d frames, dry_open=%d/%d"), *Label, RecoveryFrame, CountOpenDryCells(DryCells, Frame), DryCells.Num()));
	if (RecoveryFrame == INDEX_NONE)
	{
		AddError(MakeNoWaterActivityMessage(Label + TEXT(" (b)"), TEXT("flow")));
	}
	TestEqual(*(Label + TEXT(" (b) dry bank cells stay solid after the recovery")), static_cast<int32>(CountOpenDryCells(DryCells, Frame)), 0);
	TestEqual(*(Label + TEXT(" (b) generation is unchanged at the end")), static_cast<int32>(Component.GetSimulationGeneration()), static_cast<int32>(InitialGeneration));
	return true;
}

#endif
