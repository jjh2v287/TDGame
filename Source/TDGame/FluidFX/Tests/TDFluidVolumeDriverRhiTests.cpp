#if WITH_DEV_AUTOMATION_TESTS

#include "Components/HeterogeneousVolumeComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "FluidFX/TDFluidRenderProfile.h"
#include "FluidFX/TDFluidVolumeDriverComponent.h"
#include "FluidFX/TDFluidVolumeDriverTypes.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFluidVolumeDriverRhiTests, Log, All);

namespace TDFluidVolumeDriverRhiTestPrivate
{
	constexpr float DriverStepSeconds = 1.0f / 30.0f;
	constexpr int32 DriverSettleStepLimit = 60;
	constexpr double BoxTopCm = 100.0;
	constexpr double BoxEdgeCm = 100.0;
	constexpr double HeightToleranceCm = 1.0;
	constexpr double MissBelowOwnerCm = 1000.0;
	const FName FogTerrainTag(TEXT("TDFluidTerrain"));
	constexpr double ViewYawStepDegrees = 10.0;
	constexpr int32 CostWarmupFrameCount = 120;
	constexpr int32 CostMeasuredFrameCount = 240;
	constexpr int32 CostCaptureWidth = 1920;
	constexpr int32 CostCaptureHeight = 1080;
	constexpr double CostWindowExtentCm = 700.0;
	const FVector CostCameraLocation(-1100.0, 0.0, 450.0);

	bool CanRunVolumeDriverRhiTest(FAutomationTestBase& Test, const TCHAR* TestLabel)
	{
		const bool bHasRenderingRhi = !GUsingNullRHI && FApp::CanEverRender();
		UE_LOG(LogTDFluidVolumeDriverRhiTests, Display, TEXT("%s: %s (NullRHI=%d, CanEverRender=%d)"), TestLabel, bHasRenderingRhi ? TEXT("RUN") : TEXT("NOT RUNNABLE"), GUsingNullRHI ? 1 : 0, FApp::CanEverRender() ? 1 : 0);
		if (bHasRenderingRhi)
		{
			return true;
		}
		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), TestLabel));
		return false;
	}

	struct FDriverRhiTestWorld
	{
		UWorld* World = nullptr;

		FDriverRhiTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidVolumeDriverRhiWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FDriverRhiTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			FlushRenderingCommands();
		}

		UTDFluidLiveSubsystem* GetFluidSubsystem() const
		{
			return World->GetSubsystem<UTDFluidLiveSubsystem>();
		}

		void StepWorld(float DeltaSeconds)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
			FlushRenderingCommands();
		}
	};

	ATDFluidLiveActor* SpawnKindLiveActor(FDriverRhiTestWorld& TestWorld, ETDFluidPresetKind Kind, const FVector& Location)
	{
		UTDFluidLiveSubsystem* Subsystem = TestWorld.GetFluidSubsystem();
		if (!Subsystem)
		{
			return nullptr;
		}
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, Kind);
		return Subsystem->SpawnPooledFluidActor(Preset, Location, 120.0f);
	}

	const TCHAR* FindStageHvolPresetPath(ETDFluidPresetKind Kind)
	{
		if (Kind == ETDFluidPresetKind::SmokeHeterogeneous)
		{
			return TEXT("/Game/FluidFX/Presets/DA_TDFluid_HvolSlope.DA_TDFluid_HvolSlope");
		}
		if (Kind == ETDFluidPresetKind::SmokeCameraFacing)
		{
			return TEXT("/Game/FluidFX/Presets/DA_TDFluid_HvolCameraFacing.DA_TDFluid_HvolCameraFacing");
		}
		return nullptr;
	}

	UTDFluidPresetAsset* LoadStageHvolPreset(ETDFluidPresetKind Kind)
	{
		const TCHAR* PresetPath = FindStageHvolPresetPath(Kind);
		if (!PresetPath || !FPackageName::DoesPackageExist(FSoftObjectPath(PresetPath).GetLongPackageName()))
		{
			return nullptr;
		}
		return LoadObject<UTDFluidPresetAsset>(nullptr, PresetPath);
	}

	ATDFluidLiveActor* SpawnStageHvolLiveActor(FDriverRhiTestWorld& TestWorld, ETDFluidPresetKind Kind, const FVector& Location)
	{
		UTDFluidLiveSubsystem* Subsystem = TestWorld.GetFluidSubsystem();
		UTDFluidPresetAsset* Preset = LoadStageHvolPreset(Kind);
		if (!Subsystem || !Preset)
		{
			return nullptr;
		}
		return Subsystem->SpawnPooledFluidActor(Preset, Location, 120.0f);
	}

	bool StepUntil(FDriverRhiTestWorld& TestWorld, TFunctionRef<bool()> IsReady)
	{
		for (int32 StepIndex = 0; StepIndex < DriverSettleStepLimit; ++StepIndex)
		{
			TestWorld.StepWorld(DriverStepSeconds);
			if (IsReady())
			{
				return true;
			}
		}
		return false;
	}

	AActor* SpawnBox(UWorld& World, UStaticMesh& CubeMesh, const FVector& TopCenterCm)
	{
		AActor* BoxActor = World.SpawnActor<AActor>();
		UStaticMeshComponent* BoxMesh = NewObject<UStaticMeshComponent>(BoxActor);
		BoxMesh->SetStaticMesh(&CubeMesh);
		BoxMesh->SetMobility(EComponentMobility::Movable);
		BoxActor->SetRootComponent(BoxMesh);
		BoxMesh->RegisterComponent();
		BoxActor->SetActorLocation(TopCenterCm - FVector(0.0, 0.0, 0.5 * BoxEdgeCm));
		return BoxActor;
	}

	FIntPoint ResolveWindowPixel(const FVector4f& Window, int32 Resolution, const FVector2D& WorldXY)
	{
		const double U = (WorldXY.X - Window.X) / FMath::Max(static_cast<double>(Window.Z), 1.0) + 0.5;
		const double V = (WorldXY.Y - Window.Y) / FMath::Max(static_cast<double>(Window.W), 1.0) + 0.5;
		return FIntPoint(
			FMath::Clamp(FMath::FloorToInt32(U * Resolution), 0, Resolution - 1),
			FMath::Clamp(FMath::FloorToInt32(V * Resolution), 0, Resolution - 1));
	}

	double DecodeHeight(const TArray<FLinearColor>& Pixels, int32 Resolution, const FIntPoint& Pixel, const FVector4f& Decode)
	{
		const float Raw = Pixels[Pixel.Y * Resolution + Pixel.X].R;
		return static_cast<double>(Raw) * Decode.X + Decode.Y;
	}

	FVector ResolvePlaneCenterWorld(const ATDFluidLiveActor& LiveActor)
	{
		const UTDFluidLiveComponent& Component = *LiveActor.LiveComponent;
		const FVector2D CenterCm = Component.GetSimulationCenterWorld();
		return Component.GetPlaneBasis().IsWorldFacing()
			? FVector(CenterCm.X, CenterCm.Y, LiveActor.GetActorLocation().Z)
			: Component.UnprojectSolverPlaneToWorld(CenterCm);
	}

	void PreloadVolumeDriverAssets(const UTDFluidPresetAsset& Preset)
	{
		for (const TArray<TSoftObjectPtr<UMaterialInterface>>* SlotMaterials : { &Preset.PrimaryOutputMaterials, &Preset.SecondaryOutputMaterials, &Preset.TertiaryOutputMaterials })
		{
			for (const TSoftObjectPtr<UMaterialInterface>& SlotMaterial : *SlotMaterials)
			{
				SlotMaterial.LoadSynchronous();
			}
		}
		Preset.VolumeMesh.LoadSynchronous();
	}

	bool HasVolumeSlotAsset(ETDFluidPresetKind Kind, FString& OutDescription)
	{
		const UTDFluidPresetAsset* Preset = LoadStageHvolPreset(Kind);
		if (!Preset)
		{
			OutDescription = FString::Printf(TEXT("stage preset '%s' is missing"), FindStageHvolPresetPath(Kind) ? FindStageHvolPresetPath(Kind) : TEXT("none"));
			return false;
		}
		PreloadVolumeDriverAssets(*Preset);
		const FTDFluidVolumeDriverSettings DriverSettings = UTDFluidPresetAsset::BuildVolumeDriverForPresetKind(Kind);
		const TSoftObjectPtr<UMaterialInterface> SlotMaterial = Preset->ResolveSlotMaterial(DriverSettings.HeterogeneousVolumeMaterialSlot, 0);
		const FString PackageName = SlotMaterial.ToSoftObjectPath().GetLongPackageName();
		OutDescription = FString::Printf(TEXT("HVOL %d, slot %d, material '%s'"), DriverSettings.bRenderHeterogeneousVolume ? 1 : 0, static_cast<int32>(DriverSettings.HeterogeneousVolumeMaterialSlot), *SlotMaterial.ToString());
		return DriverSettings.bRenderHeterogeneousVolume && !PackageName.IsEmpty() && FPackageName::DoesPackageExist(PackageName);
	}

	bool WaitForVolumeMaterial(FAutomationTestBase& Test, FDriverRhiTestWorld& TestWorld, const ATDFluidLiveActor& LiveActor, const FString& Label)
	{
		const bool bIsReady = StepUntil(TestWorld, [&LiveActor]()
		{
			FlushAsyncLoading();
			const UTDFluidVolumeDriverComponent* Driver = LiveActor.VolumeDriver;
			const UHeterogeneousVolumeComponent* Volume = Driver ? Driver->GetInternalHeterogeneousVolume() : nullptr;
			return LiveActor.LiveComponent->IsSimulationInitialized() && Volume && Volume->GetMaterial(0) != nullptr;
		});
		return Test.TestTrue(*FString::Printf(TEXT("%s drives an internal HVOL with a volume material"), *Label), bIsReady);
	}

	void ExpectVolumeFollowsPlacement(FAutomationTestBase& Test, const ATDFluidLiveActor& LiveActor, const FString& Label)
	{
		const UTDFluidVolumeDriverComponent& Driver = *LiveActor.VolumeDriver;
		const UTDFluidLiveComponent& Component = *LiveActor.LiveComponent;
		const UHeterogeneousVolumeComponent& Volume = *Driver.GetInternalHeterogeneousVolume();
		const FTDFluidVolumeDriverSettings& Settings = Driver.GetDriverSettings();
		const FTDFluidVolumePlacement Placement = UTDFluidVolumeDriverComponent::ComputeHeterogeneousVolumePlacement(Component.GetPlaneBasis(), ResolvePlaneCenterWorld(LiveActor), Component.GetSimulationExtentCm(), Settings);
		const FTransform VolumeTransform = Volume.GetComponentTransform();

		Test.TestTrue(*FString::Printf(TEXT("%s volume resolution matches the placement (%s vs %s)"), *Label, *Volume.VolumeResolution.ToString(), *Placement.Resolution.ToString()), Volume.VolumeResolution == Placement.Resolution);
		Test.TestTrue(*FString::Printf(TEXT("%s volume location matches (%s vs %s)"), *Label, *VolumeTransform.GetLocation().ToString(), *Placement.Transform.GetLocation().ToString()), VolumeTransform.GetLocation().Equals(Placement.Transform.GetLocation(), 0.1));
		Test.TestTrue(*(Label + TEXT(" volume rotation matches the basis")), VolumeTransform.GetRotation().Equals(Placement.Transform.GetRotation(), 1.0e-3));
		Test.TestTrue(*(Label + TEXT(" volume scale matches the bounds")), VolumeTransform.GetScale3D().Equals(Placement.Transform.GetScale3D(), 1.0e-3));

		UMaterialInstanceDynamic* SlotMID = Driver.GetOutputMaterialInstance(Settings.HeterogeneousVolumeMaterialSlot);
		Test.TestTrue(*(Label + TEXT(" volume renders the slot MID")), SlotMID != nullptr && Volume.GetMaterial(0) == SlotMID);
		if (!SlotMID)
		{
			return;
		}
		float VolumeHeightCm = 0.0f;
		if (SlotMID->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(TEXT("VolumeHeightCm"))), VolumeHeightCm))
		{
			Test.TestEqual(*(Label + TEXT(" MID volume height")), VolumeHeightCm, Settings.HeterogeneousVolumeHeightCm, 1.0e-3f);
		}
		FLinearColor VolumeOffsetCm = FLinearColor::Black;
		if (SlotMID->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(TEXT("VolumeOffsetCm"))), VolumeOffsetCm))
		{
			const FVector ExpectedOffsetCm = UTDFluidVolumeDriverComponent::ComputeWorldOffsetCm(Component.GetPlaneBasis(), Settings.HeterogeneousVolumeOffsetCm);
			Test.TestTrue(*(Label + TEXT(" MID volume offset")), FVector(VolumeOffsetCm.R, VolumeOffsetCm.G, VolumeOffsetCm.B).Equals(ExpectedOffsetCm, 1.0e-2));
		}
	}

	struct FCostPassResult
	{
		TArray<double> GpuMilliseconds;
		double MedianGpuMilliseconds = 0.0;
		double MeanGpuMilliseconds = 0.0;
	};

	double ComputeMedian(TArray<double> Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		Values.Sort();
		return Values[Values.Num() / 2];
	}

	FCostPassResult MeasureCapturePass(FDriverRhiTestWorld& TestWorld, ATDFluidLiveActor& LiveActor, USceneCaptureComponent2D& Capture)
	{
		using FQueryArray = TArray<FRenderQueryRHIRef>;
		TSharedRef<FQueryArray, ESPMode::ThreadSafe> StartQueries = MakeShared<FQueryArray, ESPMode::ThreadSafe>();
		TSharedRef<FQueryArray, ESPMode::ThreadSafe> EndQueries = MakeShared<FQueryArray, ESPMode::ThreadSafe>();
		FTDFluidBrushStamp SourceStamp;
		SourceStamp.WorldLocation = LiveActor.GetActorLocation();
		SourceStamp.RadiusCm = 90.0f;
		SourceStamp.DensityStrength = 0.8f;
		for (int32 FrameIndex = 0; FrameIndex < CostWarmupFrameCount + CostMeasuredFrameCount; ++FrameIndex)
		{
			LiveActor.LiveComponent->QueueBrushStamp(SourceStamp);
			++GFrameCounter;
			TestWorld.World->Tick(LEVELTICK_All, DriverStepSeconds);
			const bool bIsMeasured = FrameIndex >= CostWarmupFrameCount;
			if (bIsMeasured)
			{
				ENQUEUE_RENDER_COMMAND(TDFluidHvolCostStart)([StartQueries](FRHICommandListImmediate& RHICmdList)
				{
					RHICmdList.EndRenderQuery(StartQueries->Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
				});
			}
			Capture.CaptureScene();
			if (bIsMeasured)
			{
				ENQUEUE_RENDER_COMMAND(TDFluidHvolCostEnd)([EndQueries](FRHICommandListImmediate& RHICmdList)
				{
					RHICmdList.EndRenderQuery(EndQueries->Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime)));
					RHICmdList.ImmediateFlush(EImmediateFlushType::DispatchToRHIThread);
				});
			}
			FlushRenderingCommands();
		}

		TSharedRef<TArray<double>, ESPMode::ThreadSafe> Milliseconds = MakeShared<TArray<double>, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidHvolCostRead)([StartQueries, EndQueries, Milliseconds](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.SubmitAndBlockUntilGPUIdle();
			for (int32 QueryIndex = 0; QueryIndex < FMath::Min(StartQueries->Num(), EndQueries->Num()); ++QueryIndex)
			{
				uint64 StartMicroseconds = 0;
				uint64 EndMicroseconds = 0;
				const bool bHasStart = RHIGetRenderQueryResult((*StartQueries)[QueryIndex], StartMicroseconds, true);
				const bool bHasEnd = RHIGetRenderQueryResult((*EndQueries)[QueryIndex], EndMicroseconds, true);
				if (bHasStart && bHasEnd && EndMicroseconds >= StartMicroseconds)
				{
					Milliseconds->Add(static_cast<double>(EndMicroseconds - StartMicroseconds) / 1000.0);
				}
			}
		});
		FlushRenderingCommands();

		FCostPassResult Result;
		Result.GpuMilliseconds = *Milliseconds;
		Result.MedianGpuMilliseconds = ComputeMedian(Result.GpuMilliseconds);
		double Sum = 0.0;
		for (const double Value : Result.GpuMilliseconds)
		{
			Sum += Value;
		}
		Result.MeanGpuMilliseconds = Result.GpuMilliseconds.IsEmpty() ? 0.0 : Sum / Result.GpuMilliseconds.Num();
		return Result;
	}

	double MeasureDriveMilliseconds(UTDFluidVolumeDriverComponent& Driver, const UTDFluidLiveComponent& Component)
	{
		TArray<double> Milliseconds;
		for (int32 CallIndex = 0; CallIndex < CostMeasuredFrameCount; ++CallIndex)
		{
			const double StartSeconds = FPlatformTime::Seconds();
			Driver.DriveTargets(Component, DriverStepSeconds);
			Milliseconds.Add((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		}
		FlushRenderingCommands();
		return ComputeMedian(Milliseconds);
	}

	TSharedRef<FJsonObject> CaptureRenderProfileValues()
	{
		TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
		for (const FTDFluidRenderProfileCVarBinding& Binding : FTDFluidRenderProfile::GetCVarBindings())
		{
			const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Binding.CVarName);
			Values->SetStringField(Binding.CVarName, Variable ? Variable->GetString() : FString(TEXT("missing")));
		}
		return Values;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeDriverSceneCaptureHeightRhiTest,
	"TDGame.FluidFXRhi.VolumeDriver.SceneCaptureHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeDriverSceneCaptureHeightRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumeDriverRhiTestPrivate;
	if (!CanRunVolumeDriverRhiTest(*this, TEXT("TDGame.FluidFXRhi.VolumeDriver.SceneCaptureHeight")))
	{
		return false;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine cube mesh is available"), CubeMesh))
	{
		return false;
	}

	FDriverRhiTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnKindLiveActor(TestWorld, ETDFluidPresetKind::FloorSmokeCapture, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned the floor smoke capture actor"), LiveActor))
	{
		return false;
	}
	const bool bHasFirstCapture = StepUntil(TestWorld, [LiveActor]()
	{
		return LiveActor->VolumeDriver && LiveActor->VolumeDriver->GetHeightCaptureCount() > 0;
	});
	if (!TestTrue(TEXT("The driver captures the floor height once the simulation runs"), bHasFirstCapture))
	{
		return false;
	}

	UTDFluidVolumeDriverComponent& Driver = *LiveActor->VolumeDriver;
	const FVector4f FirstWindow = Driver.GetTerrainHeightWindow();
	const FVector2D HitXY(FirstWindow.X + 0.2 * FirstWindow.Z, FirstWindow.Y + 0.1 * FirstWindow.W);
	const FVector2D MirrorXY(FirstWindow.X - 0.2 * FirstWindow.Z, FirstWindow.Y - 0.1 * FirstWindow.W);
	const FVector2D UntaggedXY(FirstWindow.X - 0.2 * FirstWindow.Z, FirstWindow.Y + 0.2 * FirstWindow.W);
	SpawnBox(*TestWorld.World, *CubeMesh, FVector(HitXY.X, HitXY.Y, BoxTopCm))->Tags.Add(FogTerrainTag);
	SpawnBox(*TestWorld.World, *CubeMesh, FVector(UntaggedXY.X, UntaggedXY.Y, BoxTopCm));

	const int32 CaptureCountBefore = Driver.GetHeightCaptureCount();
	TestWorld.StepWorld(DriverStepSeconds);
	TestEqual(TEXT("A still window does not recapture"), Driver.GetHeightCaptureCount(), CaptureCountBefore);
	Driver.RequestHeightRecapture();
	const bool bHasRecaptured = StepUntil(TestWorld, [&Driver, CaptureCountBefore]()
	{
		return Driver.GetHeightCaptureCount() > CaptureCountBefore;
	});
	if (!TestTrue(TEXT("A requested recapture runs once"), bHasRecaptured))
	{
		return false;
	}
	TestEqual(TEXT("The requested recapture runs exactly once"), Driver.GetHeightCaptureCount(), CaptureCountBefore + 1);

	UTextureRenderTarget2D* CaptureTarget = Driver.GetHeightCaptureTarget();
	FTextureRenderTargetResource* CaptureResource = CaptureTarget ? CaptureTarget->GameThread_GetRenderTargetResource() : nullptr;
	if (!TestNotNull(TEXT("The capture render target has a resource"), CaptureResource))
	{
		return false;
	}
	FlushRenderingCommands();
	TArray<FLinearColor> Pixels;
	if (!TestTrue(TEXT("Read back the R32F height capture"), CaptureResource->ReadLinearColorPixels(Pixels)))
	{
		return false;
	}
	const int32 Resolution = CaptureTarget->SizeX;
	if (!TestEqual(TEXT("Capture is square"), Pixels.Num(), Resolution * Resolution))
	{
		return false;
	}

	const FVector4f Window = Driver.GetTerrainHeightWindow();
	const FVector4f Decode = Driver.GetTerrainHeightDecode();
	const FIntPoint HitPixel = ResolveWindowPixel(Window, Resolution, HitXY);
	const FIntPoint MirrorPixel = ResolveWindowPixel(Window, Resolution, MirrorXY);
	const FIntPoint UntaggedPixel = ResolveWindowPixel(Window, Resolution, UntaggedXY);
	const double HitHeightCm = DecodeHeight(Pixels, Resolution, HitPixel, Decode);
	const double MirrorHeightCm = DecodeHeight(Pixels, Resolution, MirrorPixel, Decode);
	const double UntaggedHeightCm = DecodeHeight(Pixels, Resolution, UntaggedPixel, Decode);
	const double OwnerZ = LiveActor->GetActorLocation().Z;
	AddInfo(FString::Printf(TEXT("Window (%.1f, %.1f, %.1f, %.1f), decode (%.3f, %.1f, %.1f), hit pixel (%d, %d) raw %.3f -> %.2f cm, mirror pixel (%d, %d) raw %.3f -> %.2f cm"),
		Window.X, Window.Y, Window.Z, Window.W, Decode.X, Decode.Y, Decode.Z,
		HitPixel.X, HitPixel.Y, Pixels[HitPixel.Y * Resolution + HitPixel.X].R, HitHeightCm,
		MirrorPixel.X, MirrorPixel.Y, Pixels[MirrorPixel.Y * Resolution + MirrorPixel.X].R, MirrorHeightCm));

	TestTrue(TEXT("Decode marks the capture valid"), Decode.Z > 0.5f);
	TestTrue(*FString::Printf(TEXT("Decoded box top %.2f cm is within 1 cm of %.0f cm"), HitHeightCm, BoxTopCm), FMath::Abs(HitHeightCm - BoxTopCm) <= HeightToleranceCm);
	TestTrue(*FString::Printf(TEXT("Mirrored miss decodes far below the owner (%.1f < %.1f)"), MirrorHeightCm, OwnerZ - MissBelowOwnerCm), MirrorHeightCm < OwnerZ - MissBelowOwnerCm);
	TestTrue(*FString::Printf(TEXT("Untagged box at pixel (%d, %d) is left out of the floor smoke terrain-only capture (%.1f < %.1f)"), UntaggedPixel.X, UntaggedPixel.Y, UntaggedHeightCm, OwnerZ - MissBelowOwnerCm), UntaggedHeightCm < OwnerZ - MissBelowOwnerCm);
	return true;
}

namespace TDFluidVolumeDriverRhiTestPrivate
{
	const FName FogTerrainFollowParameterName(TEXT("FogTerrainFollow"));
	const FName TerrainHeightDecodeParameterName(TEXT("TerrainHeightDecode"));
	const FName TerrainHeightTextureParameterName(TEXT("TerrainHeightTexture"));
	const TCHAR* FogSimInstancePath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidFog_Sim.MI_TD_FluidFog_Sim");
	constexpr double FogFollowToleranceCm = 2.0;
	constexpr double FogHillTopCm = 1200.0;
	constexpr double FogOverhangTopCm = 400.0;
	constexpr double FogOverhangThicknessCm = 20.0;
	constexpr double FogFeatureHalfSizeCm = 300.0;
	constexpr double FogPillarHalfSizeCm = 25.0;
	constexpr double FogOverhangSampleOffsetCm = 150.0;
	constexpr double FogFloorHalfSizeCm = 4000.0;
	constexpr double FogFloorThicknessCm = 100.0;
	constexpr float FogVolumeHeightCm = 1300.0f;
	const FVector FogStirActorLocation(0.0, 30000.0, 0.0);
	const FVector FogSmokeActorLocation(0.0, -30000.0, 0.0);

	AActor* SpawnBoxBetween(UWorld& World, UStaticMesh& CubeMesh, const FVector& MinCm, const FVector& MaxCm, FName Tag)
	{
		AActor* BoxActor = SpawnBox(World, CubeMesh, FVector::ZeroVector);
		BoxActor->SetActorScale3D((MaxCm - MinCm) / BoxEdgeCm);
		BoxActor->SetActorLocation(0.5 * (MinCm + MaxCm));
		if (!Tag.IsNone())
		{
			BoxActor->Tags.Add(Tag);
		}
		return BoxActor;
	}

	ATDFluidLiveActor* SpawnFogLiveActor(FDriverRhiTestWorld& TestWorld, ETDFluidPresetKind Kind, const FVector& Location, UMaterialInterface& FogMaterial, UStaticMesh& CubeMesh)
	{
		UTDFluidLiveSubsystem* Subsystem = TestWorld.GetFluidSubsystem();
		if (!Subsystem)
		{
			return nullptr;
		}
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, Kind);
		Preset->VolumeMaterial = &FogMaterial;
		Preset->VolumeMesh = &CubeMesh;
		Preset->VolumeHeightCm = FogVolumeHeightCm;
		return Subsystem->SpawnPooledFluidActor(Preset, Location, 120.0f);
	}

	UMaterialInstanceDynamic* FindLiveVolumeMID(const ATDFluidLiveActor& LiveActor)
	{
		return LiveActor.VolumeMeshComponent ? Cast<UMaterialInstanceDynamic>(LiveActor.VolumeMeshComponent->GetMaterial(0)) : nullptr;
	}

	const FScalarParameterValue* FindScalarOverride(const UMaterialInstanceDynamic& MID, FName ParameterName)
	{
		return MID.ScalarParameterValues.FindByPredicate([ParameterName](const FScalarParameterValue& Value) { return Value.ParameterInfo.Name == ParameterName; });
	}

	const FVectorParameterValue* FindVectorOverride(const UMaterialInstanceDynamic& MID, FName ParameterName)
	{
		return MID.VectorParameterValues.FindByPredicate([ParameterName](const FVectorParameterValue& Value) { return Value.ParameterInfo.Name == ParameterName; });
	}

	const FTextureParameterValue* FindTextureOverride(const UMaterialInstanceDynamic& MID, FName ParameterName)
	{
		return MID.TextureParameterValues.FindByPredicate([ParameterName](const FTextureParameterValue& Value) { return Value.ParameterInfo.Name == ParameterName; });
	}

	bool RecaptureAndWait(FDriverRhiTestWorld& TestWorld, UTDFluidVolumeDriverComponent& Driver)
	{
		const int32 CaptureCountBefore = Driver.GetHeightCaptureCount();
		Driver.RequestHeightRecapture();
		return StepUntil(TestWorld, [&Driver, CaptureCountBefore]()
		{
			return Driver.GetHeightCaptureCount() > CaptureCountBefore;
		});
	}

	bool ReadHeightCapture(FAutomationTestBase& Test, const UTDFluidVolumeDriverComponent& Driver, TArray<FLinearColor>& OutPixels, int32& OutResolution)
	{
		UTextureRenderTarget2D* CaptureTarget = Driver.GetHeightCaptureTarget();
		FTextureRenderTargetResource* CaptureResource = CaptureTarget ? CaptureTarget->GameThread_GetRenderTargetResource() : nullptr;
		if (!Test.TestNotNull(TEXT("The capture render target has a resource"), CaptureResource))
		{
			return false;
		}
		FlushRenderingCommands();
		if (!Test.TestTrue(TEXT("Read back the R32F height capture"), CaptureResource->ReadLinearColorPixels(OutPixels)))
		{
			return false;
		}
		OutResolution = CaptureTarget->SizeX;
		return Test.TestEqual(TEXT("Capture is square"), OutPixels.Num(), OutResolution * OutResolution);
	}

	double DecodeHeightAt(const UTDFluidVolumeDriverComponent& Driver, const TArray<FLinearColor>& Pixels, int32 Resolution, const FVector2D& WorldXY)
	{
		const FIntPoint Pixel = ResolveWindowPixel(Driver.GetTerrainHeightWindow(), Resolution, WorldXY);
		return DecodeHeight(Pixels, Resolution, Pixel, Driver.GetTerrainHeightDecode());
	}

	FString GetAllGeometryVariantTuningPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/VolumeDriverRhiTests/TDFluidFogMistAllGeometryVariant.json")));
	}

	bool WriteAllGeometryVariantTuningFile()
	{
		FString BaseText;
		if (!FFileHelper::LoadFileToString(BaseText, *UTDFluidPresetTuningSubsystem::GetTuningFilePath()))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BaseText), Root) || !Root.IsValid())
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Kinds = nullptr;
		const TSharedPtr<FJsonObject>* FogMistLayers = nullptr;
		const TSharedPtr<FJsonObject>* FogMistTerrain = nullptr;
		if (!Root->TryGetObjectField(TEXT("Kinds"), Kinds) || !(*Kinds)->TryGetObjectField(TEXT("FogMist"), FogMistLayers) || !(*FogMistLayers)->TryGetObjectField(TEXT("TDFluidTerrainHeightSettings"), FogMistTerrain))
		{
			return false;
		}
		(*FogMistTerrain)->SetStringField(TEXT("SceneCaptureContent"), TEXT("AllGeometry"));
		FString VariantText;
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&VariantText)))
		{
			return false;
		}
		return FFileHelper::SaveStringToFile(VariantText, *GetAllGeometryVariantTuningPath());
	}

	void RestoreBaseTuning()
	{
		if (UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr)
		{
			TuningSubsystem->ReloadPresetTuning();
		}
		IFileManager::Get().Delete(*GetAllGeometryVariantTuningPath());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeDriverFogFloorFollowsTerrainRhiTest,
	"TDGame.FluidFXRhi.VolumeDriver.FogFloorFollowsTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeDriverFogFloorFollowsTerrainRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumeDriverRhiTestPrivate;
	if (!CanRunVolumeDriverRhiTest(*this, TEXT("TDGame.FluidFXRhi.VolumeDriver.FogFloorFollowsTerrain")))
	{
		return false;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* FogMaterial = LoadObject<UMaterialInterface>(nullptr, FogSimInstancePath);
	if (!TestNotNull(TEXT("Engine cube mesh is available"), CubeMesh) || !TestNotNull(TEXT("MI_TD_FluidFog_Sim is available"), FogMaterial))
	{
		return false;
	}

	FDriverRhiTestWorld TestWorld;
	ATDFluidLiveActor* MistActor = SpawnFogLiveActor(TestWorld, ETDFluidPresetKind::FogMist, FVector::ZeroVector, *FogMaterial, *CubeMesh);
	ATDFluidLiveActor* StirActor = SpawnFogLiveActor(TestWorld, ETDFluidPresetKind::FogMistStir, FogStirActorLocation, *FogMaterial, *CubeMesh);
	ATDFluidLiveActor* SmokeActor = SpawnFogLiveActor(TestWorld, ETDFluidPresetKind::Smoke, FogSmokeActorLocation, *FogMaterial, *CubeMesh);
	if (!TestNotNull(TEXT("Spawned the FogMist actor"), MistActor) || !TestNotNull(TEXT("Spawned the FogMistStir actor"), StirActor) || !TestNotNull(TEXT("Spawned the Smoke actor"), SmokeActor))
	{
		return false;
	}
	const bool bHasFirstCapture = StepUntil(TestWorld, [MistActor, StirActor, SmokeActor]()
	{
		FlushAsyncLoading();
		const bool bHasMistCapture = MistActor->VolumeDriver && MistActor->VolumeDriver->GetHeightCaptureCount() > 0;
		const bool bHasStirCapture = StirActor->VolumeDriver && StirActor->VolumeDriver->GetHeightCaptureCount() > 0;
		return bHasMistCapture && bHasStirCapture && FindLiveVolumeMID(*MistActor) && FindLiveVolumeMID(*StirActor) && FindLiveVolumeMID(*SmokeActor);
	});
	if (!TestTrue(TEXT("FogMist and FogMistStir get drivers that capture the floor height, and all three fog actors get a live volume material"), bHasFirstCapture))
	{
		return false;
	}

	UTDFluidVolumeDriverComponent& Driver = *MistActor->VolumeDriver;
	const FVector4f FirstWindow = Driver.GetTerrainHeightWindow();
	const FVector2D WindowCenter(FirstWindow.X, FirstWindow.Y);
	const FVector2D HillXY = WindowCenter + FVector2D(0.2 * FirstWindow.Z, 0.1 * FirstWindow.W);
	const FVector2D OverhangXY = WindowCenter - FVector2D(0.2 * FirstWindow.Z, 0.1 * FirstWindow.W);
	const FVector2D OverhangSampleXY = OverhangXY + FVector2D(FogOverhangSampleOffsetCm, 0.0);
	const FVector2D OpenXY = WindowCenter + FVector2D(-0.2 * FirstWindow.Z, 0.3 * FirstWindow.W);
	const FVector FeatureHalf(FogFeatureHalfSizeCm, FogFeatureHalfSizeCm, 0.0);
	const FVector FloorHalf(FogFloorHalfSizeCm, FogFloorHalfSizeCm, 0.0);
	const FVector PillarHalf(FogPillarHalfSizeCm, FogPillarHalfSizeCm, 0.0);
	SpawnBoxBetween(*TestWorld.World, *CubeMesh, FVector(WindowCenter, -FogFloorThicknessCm) - FloorHalf, FVector(WindowCenter, 0.0) + FloorHalf, FogTerrainTag);
	SpawnBoxBetween(*TestWorld.World, *CubeMesh, FVector(HillXY, 0.0) - FeatureHalf, FVector(HillXY, FogHillTopCm) + FeatureHalf, FogTerrainTag);
	SpawnBoxBetween(*TestWorld.World, *CubeMesh, FVector(OverhangXY, FogOverhangTopCm - FogOverhangThicknessCm) - FeatureHalf, FVector(OverhangXY, FogOverhangTopCm) + FeatureHalf, NAME_None);
	SpawnBoxBetween(*TestWorld.World, *CubeMesh, FVector(OverhangXY, 0.0) - PillarHalf, FVector(OverhangXY, FogOverhangTopCm - FogOverhangThicknessCm) + PillarHalf, NAME_None);
	if (!TestTrue(TEXT("A requested recapture runs with the terrain in place"), RecaptureAndWait(TestWorld, Driver)))
	{
		return false;
	}

	UMaterialInstanceDynamic* MistMID = FindLiveVolumeMID(*MistActor);
	if (!TestNotNull(TEXT("FogMist keeps its live volume material"), MistMID))
	{
		return false;
	}
	const FScalarParameterValue* FollowValue = FindScalarOverride(*MistMID, FogTerrainFollowParameterName);
	const FVectorParameterValue* DecodeValue = FindVectorOverride(*MistMID, TerrainHeightDecodeParameterName);
	const FTextureParameterValue* TextureValue = FindTextureOverride(*MistMID, TerrainHeightTextureParameterName);
	TestTrue(TEXT("FogMist live volume material sets FogTerrainFollow to 1"), FollowValue && FMath::IsNearlyEqual(FollowValue->ParameterValue, 1.0f));
	TestTrue(TEXT("FogMist live volume material marks the terrain decode valid"), DecodeValue && FMath::IsNearlyEqual(DecodeValue->ParameterValue.B, 1.0f));
	TestTrue(TEXT("FogMist live volume material samples the driver capture target"), TextureValue && TextureValue->ParameterValue == Driver.GetHeightCaptureTarget());

	TArray<FLinearColor> Pixels;
	int32 Resolution = 0;
	if (!ReadHeightCapture(*this, Driver, Pixels, Resolution))
	{
		return false;
	}
	const double HillCm = DecodeHeightAt(Driver, Pixels, Resolution, HillXY);
	const double OverhangTerrainOnlyCm = DecodeHeightAt(Driver, Pixels, Resolution, OverhangSampleXY);
	const double OpenCm = DecodeHeightAt(Driver, Pixels, Resolution, OpenXY);
	const FVector4f Window = Driver.GetTerrainHeightWindow();
	const FVector4f Decode = Driver.GetTerrainHeightDecode();
	AddInfo(FString::Printf(TEXT("TDFluidFogFloorFollow: window (%.1f, %.1f, %.1f, %.1f) decode (%.3f, %.1f, %.1f) resolution %d, terrain-only hill %.2f overhang %.2f open %.2f cm"),
		Window.X, Window.Y, Window.Z, Window.W, Decode.X, Decode.Y, Decode.Z, Resolution, HillCm, OverhangTerrainOnlyCm, OpenCm));
	TestTrue(*FString::Printf(TEXT("Terrain-only capture decodes the tagged hill top %.2f cm within 2 cm of %.0f cm"), HillCm, FogHillTopCm), FMath::Abs(HillCm - FogHillTopCm) <= FogFollowToleranceCm);
	TestTrue(*FString::Printf(TEXT("Terrain-only capture skips the untagged overhang and sees the floor (%.2f cm)"), OverhangTerrainOnlyCm), FMath::Abs(OverhangTerrainOnlyCm) <= FogFollowToleranceCm);
	TestTrue(*FString::Printf(TEXT("Terrain-only capture decodes the open floor (%.2f cm)"), OpenCm), FMath::Abs(OpenCm) <= FogFollowToleranceCm);

	UMaterialInstanceDynamic* StirMID = FindLiveVolumeMID(*StirActor);
	if (TestNotNull(TEXT("FogMistStir has a live volume material"), StirMID))
	{
		const FScalarParameterValue* StirFollowValue = FindScalarOverride(*StirMID, FogTerrainFollowParameterName);
		const FTextureParameterValue* StirTextureValue = FindTextureOverride(*StirMID, TerrainHeightTextureParameterName);
		TestTrue(TEXT("FogMistStir live volume material sets FogTerrainFollow to 1"), StirFollowValue && FMath::IsNearlyEqual(StirFollowValue->ParameterValue, 1.0f));
		TestTrue(TEXT("FogMistStir live volume material samples its own driver capture target"), StirTextureValue && StirActor->VolumeDriver && StirTextureValue->ParameterValue == StirActor->VolumeDriver->GetHeightCaptureTarget());
	}
	UMaterialInstanceDynamic* SmokeMID = FindLiveVolumeMID(*SmokeActor);
	if (TestNotNull(TEXT("Smoke has a live volume material"), SmokeMID))
	{
		const FScalarParameterValue* SmokeFollowValue = FindScalarOverride(*SmokeMID, FogTerrainFollowParameterName);
		TestTrue(TEXT("Smoke leaves FogTerrainFollow unset or 0"), !SmokeFollowValue || FMath::IsNearlyZero(SmokeFollowValue->ParameterValue));
	}
	TestTrue(TEXT("Smoke builds no driver or never captures the floor height"), !SmokeActor->VolumeDriver || SmokeActor->VolumeDriver->GetHeightCaptureCount() == 0);

	ON_SCOPE_EXIT
	{
		RestoreBaseTuning();
	};
	UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("The preset tuning subsystem exists"), TuningSubsystem) || !TestTrue(TEXT("Wrote the AllGeometry FogMist tuning variant"), WriteAllGeometryVariantTuningFile()))
	{
		return false;
	}
	if (!TestEqual(TEXT("The AllGeometry FogMist variant loads without errors"), TuningSubsystem->ReloadPresetTuningFromPaths(GetAllGeometryVariantTuningPath(), FString()), 0))
	{
		return false;
	}
	const int32 CaptureCountBeforeVariant = Driver.GetHeightCaptureCount();
	const bool bHasVariantCapture = StepUntil(TestWorld, [&Driver, CaptureCountBeforeVariant]()
	{
		return Driver.GetTerrainHeightSettings().SceneCaptureContent == ETDFluidHeightCaptureContent::AllGeometry && Driver.GetHeightCaptureCount() > CaptureCountBeforeVariant;
	});
	if (!TestTrue(TEXT("Reapplying an AllGeometry FogMist table recaptures the floor height"), bHasVariantCapture) || !ReadHeightCapture(*this, Driver, Pixels, Resolution))
	{
		return false;
	}
	const double OverhangAllGeometryCm = DecodeHeightAt(Driver, Pixels, Resolution, OverhangSampleXY);
	AddInfo(FString::Printf(TEXT("TDFluidFogFloorFollow: all-geometry overhang %.2f cm"), OverhangAllGeometryCm));
	TestTrue(*FString::Printf(TEXT("All-geometry capture sees the overhang top %.2f cm within 2 cm of %.0f cm"), OverhangAllGeometryCm, FogOverhangTopCm), FMath::Abs(OverhangAllGeometryCm - FogOverhangTopCm) <= FogFollowToleranceCm);
	return true;
}

namespace TDFluidVolumeDriverRhiTestPrivate
{
	const TCHAR* FogTileSyncInstancePath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidFog_TileSync.MI_TD_FluidFog_TileSync");
	constexpr double FogTileExpectedCaptureWidthCm = 15273.5;
	constexpr double FogTileWindowToleranceCm = 1.0;
	constexpr int32 FogTileExpectedCaptureResolution = 512;
	constexpr double FogTileFloorHalfSizeCm = 8000.0;
	constexpr double FogTileHillHalfSizeCm = 100.0;
	constexpr double FogTileHillTopCm = 600.0;
	const FVector2D FogTileHillInsideBoxXY(2500.0, 0.0);
	const FVector2D FogTileBandHillAlongXXY(5900.0, 0.0);
	const FVector2D FogTileBandHillAlongYXY(0.0, -5900.0);
	const FVector2D FogTileOpenFloorXY(-2500.0, 0.0);

	FString GetFogTileFollowVariantTuningPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX/VolumeDriverRhiTests/TDFluidFogTileFollowVariant.json")));
	}

	bool WriteFogTileFollowVariantTuningFile()
	{
		FString BaseText;
		if (!FFileHelper::LoadFileToString(BaseText, *UTDFluidPresetTuningSubsystem::GetTuningFilePath()))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BaseText), Root) || !Root.IsValid())
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Kinds = nullptr;
		const TSharedPtr<FJsonObject>* FogTileLayers = nullptr;
		if (!Root->TryGetObjectField(TEXT("Kinds"), Kinds) || !(*Kinds)->TryGetObjectField(TEXT("FogTileSim"), FogTileLayers))
		{
			return false;
		}
		const TSharedRef<FJsonObject> FogTileDriver = MakeShared<FJsonObject>();
		FogTileDriver->SetBoolField(TEXT("bFogFloorFollowsTerrain"), true);
		const TSharedRef<FJsonObject> FogTileTerrain = MakeShared<FJsonObject>();
		FogTileTerrain->SetStringField(TEXT("VolumeHeightSource"), TEXT("SceneCapture"));
		FogTileTerrain->SetStringField(TEXT("SceneCaptureContent"), TEXT("TerrainOnly"));
		FogTileTerrain->SetNumberField(TEXT("SceneCaptureHeightAboveCm"), 2000.0);
		FogTileTerrain->SetNumberField(TEXT("SceneCaptureRecaptureCells"), 16);
		FogTileTerrain->SetNumberField(TEXT("SceneCaptureResolution"), FogTileExpectedCaptureResolution);
		(*FogTileLayers)->SetObjectField(TEXT("TDFluidVolumeDriverSettings"), FogTileDriver);
		(*FogTileLayers)->SetObjectField(TEXT("TDFluidTerrainHeightSettings"), FogTileTerrain);
		FString VariantText;
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&VariantText)))
		{
			return false;
		}
		return FFileHelper::SaveStringToFile(VariantText, *GetFogTileFollowVariantTuningPath());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeDriverFogTileFollowCoversBoxRhiTest,
	"TDGame.FluidFXRhi.VolumeDriver.FogTileFollowCoversBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeDriverFogTileFollowCoversBoxRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumeDriverRhiTestPrivate;
	if (!CanRunVolumeDriverRhiTest(*this, TEXT("TDGame.FluidFXRhi.VolumeDriver.FogTileFollowCoversBox")))
	{
		return false;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* FogMaterial = LoadObject<UMaterialInterface>(nullptr, FogTileSyncInstancePath);
	if (!TestNotNull(TEXT("Engine cube mesh is available"), CubeMesh) || !TestNotNull(TEXT("MI_TD_FluidFog_TileSync is available"), FogMaterial))
	{
		return false;
	}

	UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("The preset tuning subsystem exists"), TuningSubsystem) || !TestTrue(TEXT("Wrote the FogTileSim follow tuning variant"), WriteFogTileFollowVariantTuningFile()))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		TuningSubsystem->ReloadPresetTuning();
		IFileManager::Get().Delete(*GetFogTileFollowVariantTuningPath());
	};
	if (!TestEqual(TEXT("The FogTileSim follow variant loads without errors"), TuningSubsystem->ReloadPresetTuningFromPaths(GetFogTileFollowVariantTuningPath(), FString()), 0))
	{
		return false;
	}
	if (!TestTrue(TEXT("The FogTileSim follow variant turns following on"), UTDFluidPresetAsset::BuildVolumeDriverForPresetKind(ETDFluidPresetKind::FogTileSim).bFogFloorFollowsTerrain))
	{
		return false;
	}

	FDriverRhiTestWorld TestWorld;
	ATDFluidLiveActor* TileActor = SpawnFogLiveActor(TestWorld, ETDFluidPresetKind::FogTileSim, FVector::ZeroVector, *FogMaterial, *CubeMesh);
	if (!TestNotNull(TEXT("Spawned the FogTileSim actor"), TileActor))
	{
		return false;
	}
	const bool bHasFirstCapture = StepUntil(TestWorld, [TileActor]()
	{
		FlushAsyncLoading();
		const bool bHasTileCapture = TileActor->VolumeDriver && TileActor->VolumeDriver->GetHeightCaptureCount() > 0;
		return bHasTileCapture && FindLiveVolumeMID(*TileActor);
	});
	if (!TestTrue(TEXT("FogTileSim gets a driver that captures the floor height and a live volume material"), bHasFirstCapture))
	{
		return false;
	}

	const FVector FloorHalf(FogTileFloorHalfSizeCm, FogTileFloorHalfSizeCm, 0.0);
	const FVector HillHalf(FogTileHillHalfSizeCm, FogTileHillHalfSizeCm, 0.0);
	const FVector2D HillPoints[] = { FogTileHillInsideBoxXY, FogTileBandHillAlongXXY, FogTileBandHillAlongYXY };
	SpawnBoxBetween(*TestWorld.World, *CubeMesh, FVector(0.0, 0.0, -FogFloorThicknessCm) - FloorHalf, FloorHalf, FogTerrainTag);
	for (const FVector2D& HillXY : HillPoints)
	{
		SpawnBoxBetween(*TestWorld.World, *CubeMesh, FVector(HillXY, 0.0) - HillHalf, FVector(HillXY, FogTileHillTopCm) + HillHalf, FogTerrainTag);
	}
	UTDFluidVolumeDriverComponent& Driver = *TileActor->VolumeDriver;
	if (!TestTrue(TEXT("A requested recapture runs with the terrain in place"), RecaptureAndWait(TestWorld, Driver)))
	{
		return false;
	}

	const FVector4f Window = Driver.GetTerrainHeightWindow();
	TestTrue(*FString::Printf(TEXT("FogTileSim capture stays centred on the simulation (%.1f, %.1f)"), Window.X, Window.Y), FMath::Abs(Window.X) <= FogTileWindowToleranceCm && FMath::Abs(Window.Y) <= FogTileWindowToleranceCm);
	TestTrue(*FString::Printf(TEXT("FogTileSim capture width %.1f x %.1f cm is %.1f cm within 1 cm"), Window.Z, Window.W, FogTileExpectedCaptureWidthCm), FMath::Abs(Window.Z - FogTileExpectedCaptureWidthCm) <= FogTileWindowToleranceCm && FMath::Abs(Window.W - FogTileExpectedCaptureWidthCm) <= FogTileWindowToleranceCm);

	TArray<FLinearColor> Pixels;
	int32 Resolution = 0;
	if (!ReadHeightCapture(*this, Driver, Pixels, Resolution))
	{
		return false;
	}
	TestEqual(TEXT("FogTileSim captures at 512 x 512"), Resolution, FogTileExpectedCaptureResolution);

	const UStaticMeshComponent* VolumeMesh = TileActor->VolumeMeshComponent;
	if (TestNotNull(TEXT("FogTileSim has a live volume mesh"), VolumeMesh))
	{
		const double SphereRadiusCm = VolumeMesh->Bounds.SphereRadius;
		const double CaptureHalfWidthCm = 0.5 * Window.Z;
		AddInfo(FString::Printf(TEXT("TDFluidFogTileCover: volume box extent (%.1f, %.1f, %.1f) sphere radius %.1f cm, capture half width %.1f cm"),
			VolumeMesh->Bounds.BoxExtent.X, VolumeMesh->Bounds.BoxExtent.Y, VolumeMesh->Bounds.BoxExtent.Z, SphereRadiusCm, CaptureHalfWidthCm));
		TestTrue(*FString::Printf(TEXT("Capture half width %.1f cm covers the volume bounding sphere radius %.1f cm"), CaptureHalfWidthCm, SphereRadiusCm), CaptureHalfWidthCm >= SphereRadiusCm);
	}

	const double InsideBoxCm = DecodeHeightAt(Driver, Pixels, Resolution, FogTileHillInsideBoxXY);
	const double BandAlongXCm = DecodeHeightAt(Driver, Pixels, Resolution, FogTileBandHillAlongXXY);
	const double BandAlongYCm = DecodeHeightAt(Driver, Pixels, Resolution, FogTileBandHillAlongYXY);
	const double OpenFloorCm = DecodeHeightAt(Driver, Pixels, Resolution, FogTileOpenFloorXY);
	AddInfo(FString::Printf(TEXT("TDFluidFogTileCover: window (%.1f, %.1f, %.1f, %.1f) resolution %d, hill in box %.2f, band x %.2f, band y %.2f, open %.2f cm"),
		Window.X, Window.Y, Window.Z, Window.W, Resolution, InsideBoxCm, BandAlongXCm, BandAlongYCm, OpenFloorCm));
	TestTrue(*FString::Printf(TEXT("Capture decodes the hill outside the sim window inside the box (%.2f cm) within 2 cm of %.0f cm"), InsideBoxCm, FogTileHillTopCm), FMath::Abs(InsideBoxCm - FogTileHillTopCm) <= FogFollowToleranceCm);
	TestTrue(*FString::Printf(TEXT("Capture decodes the hill beyond the box along X (%.2f cm) within 2 cm of %.0f cm"), BandAlongXCm, FogTileHillTopCm), FMath::Abs(BandAlongXCm - FogTileHillTopCm) <= FogFollowToleranceCm);
	TestTrue(*FString::Printf(TEXT("Capture decodes the hill beyond the box along Y (%.2f cm) within 2 cm of %.0f cm"), BandAlongYCm, FogTileHillTopCm), FMath::Abs(BandAlongYCm - FogTileHillTopCm) <= FogFollowToleranceCm);
	TestTrue(*FString::Printf(TEXT("Capture decodes the open floor (%.2f cm)"), OpenFloorCm), FMath::Abs(OpenFloorCm) <= FogFollowToleranceCm);

	UMaterialInstanceDynamic* TileMID = FindLiveVolumeMID(*TileActor);
	if (TestNotNull(TEXT("FogTileSim keeps its live volume material"), TileMID))
	{
		const FScalarParameterValue* FollowValue = FindScalarOverride(*TileMID, FogTerrainFollowParameterName);
		TestTrue(TEXT("FogTileSim live volume material sets FogTerrainFollow to 1"), FollowValue && FMath::IsNearlyEqual(FollowValue->ParameterValue, 1.0f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeDriverHeterogeneousDrivenRhiTest,
	"TDGame.FluidFXRhi.VolumeDriver.HeterogeneousDriven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeDriverHeterogeneousDrivenRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumeDriverRhiTestPrivate;
	if (!CanRunVolumeDriverRhiTest(*this, TEXT("TDGame.FluidFXRhi.VolumeDriver.HeterogeneousDriven")))
	{
		return false;
	}

	FString WorldAssetDescription;
	FString CameraAssetDescription;
	const bool bHasWorldAsset = HasVolumeSlotAsset(ETDFluidPresetKind::SmokeHeterogeneous, WorldAssetDescription);
	const bool bHasCameraAsset = HasVolumeSlotAsset(ETDFluidPresetKind::SmokeCameraFacing, CameraAssetDescription);
	if (!bHasWorldAsset || !bHasCameraAsset)
	{
		AddError(FString::Printf(TEXT("the HVOL slot assets are missing (run the HVOL material and stage scripts): SmokeHeterogeneous %s; SmokeCameraFacing %s"), *WorldAssetDescription, *CameraAssetDescription));
		return false;
	}

	FDriverRhiTestWorld TestWorld;
	UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.GetFluidSubsystem();
	if (!TestNotNull(TEXT("The world has a fluid subsystem"), FluidSubsystem))
	{
		return false;
	}
	const FRotator InitialViewRotation(-60.0, 0.0, 0.0);
	FluidSubsystem->SetPlaneViewRotationOverrideForTests(InitialViewRotation);

	ATDFluidLiveActor* WorldActor = SpawnStageHvolLiveActor(TestWorld, ETDFluidPresetKind::SmokeHeterogeneous, FVector(0.0, 0.0, 50.0));
	if (!TestNotNull(TEXT("Spawned the world-facing HVOL actor"), WorldActor) || !WaitForVolumeMaterial(*this, TestWorld, *WorldActor, TEXT("World-facing")))
	{
		return false;
	}
	ExpectVolumeFollowsPlacement(*this, *WorldActor, TEXT("World-facing"));
	const FBox VolumeBounds = WorldActor->VolumeDriver->GetInternalHeterogeneousVolume()->Bounds.GetBox();
	const FVector2D ExtentCm = WorldActor->LiveComponent->GetSimulationExtentCm();
	TestTrue(*FString::Printf(TEXT("World-facing bounds cover the window (%s vs %s)"), *VolumeBounds.GetSize().ToString(), *ExtentCm.ToString()), VolumeBounds.GetSize().X >= ExtentCm.X - 1.0 && VolumeBounds.GetSize().Y >= ExtentCm.Y - 1.0);

	ATDFluidLiveActor* CameraActor = SpawnStageHvolLiveActor(TestWorld, ETDFluidPresetKind::SmokeCameraFacing, FVector(3000.0, 0.0, 50.0));
	if (!TestNotNull(TEXT("Spawned the camera-facing HVOL actor"), CameraActor) || !WaitForVolumeMaterial(*this, TestWorld, *CameraActor, TEXT("Camera-facing")))
	{
		return false;
	}
	ExpectVolumeFollowsPlacement(*this, *CameraActor, TEXT("Camera-facing before rotation"));
	const FQuat RotationBefore = CameraActor->VolumeDriver->GetInternalHeterogeneousVolume()->GetComponentQuat();

	FluidSubsystem->SetPlaneViewRotationOverrideForTests(InitialViewRotation + FRotator(0.0, ViewYawStepDegrees, 0.0));
	for (int32 StepIndex = 0; StepIndex < 4; ++StepIndex)
	{
		TestWorld.StepWorld(DriverStepSeconds);
	}
	ExpectVolumeFollowsPlacement(*this, *CameraActor, TEXT("Camera-facing after rotation"));
	const FQuat RotationAfter = CameraActor->VolumeDriver->GetInternalHeterogeneousVolume()->GetComponentQuat();
	const double TurnedDegrees = FMath::RadiansToDegrees(RotationBefore.AngularDistance(RotationAfter));
	TestTrue(*FString::Printf(TEXT("The volume turns with the 10 degree view yaw (%.2f degrees)"), TurnedDegrees), FMath::Abs(TurnedDegrees - ViewYawStepDegrees) <= 0.5);
	FluidSubsystem->ClearPlaneViewRotationOverrideForTests();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeDriverOffscreenCostRhiTest,
	"TDGame.FluidFXRhi.VolumeDriver.OffscreenCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeDriverOffscreenCostRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumeDriverRhiTestPrivate;
	if (!CanRunVolumeDriverRhiTest(*this, TEXT("TDGame.FluidFXRhi.VolumeDriver.OffscreenCost")))
	{
		return false;
	}

	FString AssetDescription;
	if (!HasVolumeSlotAsset(ETDFluidPresetKind::SmokeHeterogeneous, AssetDescription))
	{
		AddError(FString::Printf(TEXT("the HVOL slot asset is missing (run the HVOL material and stage scripts): SmokeHeterogeneous %s"), *AssetDescription));
		return false;
	}

	FDriverRhiTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnStageHvolLiveActor(TestWorld, ETDFluidPresetKind::SmokeHeterogeneous, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned the HVOL cost actor"), LiveActor) || !WaitForVolumeMaterial(*this, TestWorld, *LiveActor, TEXT("Cost")))
	{
		return false;
	}
	UTDFluidVolumeDriverComponent& Driver = *LiveActor->VolumeDriver;
	const FString VolumeResolutionText = Driver.GetInternalHeterogeneousVolume()->VolumeResolution.ToString();
	const FVector2D ExtentCm = LiveActor->LiveComponent->GetSimulationExtentCm();
	TestEqual(TEXT("Cost window is 7 m"), ExtentCm.X, CostWindowExtentCm, 1.0);

	UTextureRenderTarget2D* ColorTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	TStrongObjectPtr<UTextureRenderTarget2D> ColorTargetGuard(ColorTarget);
	ColorTarget->RenderTargetFormat = RTF_RGBA16f;
	ColorTarget->InitAutoFormat(CostCaptureWidth, CostCaptureHeight);
	ColorTarget->UpdateResourceImmediate(true);

	AActor* CameraHost = TestWorld.World->SpawnActor<AActor>();
	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(CameraHost);
	CameraHost->SetRootComponent(Capture);
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->TextureTarget = ColorTarget;
	Capture->RegisterComponent();
	Capture->SetWorldLocationAndRotation(CostCameraLocation, (LiveActor->GetActorLocation() - CostCameraLocation).Rotation());

	const TSharedRef<FJsonObject> ProfileWhileDriving = CaptureRenderProfileValues();
	const int32 OverriddenWhileDriving = FTDFluidRenderProfile::GetOverriddenCVarCount();
	const FCostPassResult VolumeOn = MeasureCapturePass(TestWorld, *LiveActor, *Capture);
	const double DriveMilliseconds = MeasureDriveMilliseconds(Driver, *LiveActor->LiveComponent);
	Driver.SetDriverHidden(true);
	const FCostPassResult VolumeOff = MeasureCapturePass(TestWorld, *LiveActor, *Capture);
	Driver.SetDriverHidden(false);

	TestTrue(TEXT("Measured every volume-on frame"), VolumeOn.GpuMilliseconds.Num() == CostMeasuredFrameCount);
	TestTrue(TEXT("Measured every volume-off frame"), VolumeOff.GpuMilliseconds.Num() == CostMeasuredFrameCount);

	LiveActor->VolumeDriver->DestroyComponent();
	LiveActor->VolumeDriver = nullptr;
	const TSharedRef<FJsonObject> ProfileAfterRelease = CaptureRenderProfileValues();

	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("test"), TEXT("TDGame.FluidFXRhi.VolumeDriver.OffscreenCost"));
	Report->SetStringField(TEXT("rhi"), GDynamicRHI ? FString(GDynamicRHI->GetName()) : FString(TEXT("none")));
	Report->SetStringField(TEXT("recorded_at"), FDateTime::Now().ToIso8601());
	Report->SetNumberField(TEXT("window_cm"), ExtentCm.X);
	Report->SetStringField(TEXT("volume_resolution"), VolumeResolutionText);
	Report->SetStringField(TEXT("capture"), FString::Printf(TEXT("%dx%d"), CostCaptureWidth, CostCaptureHeight));
	Report->SetNumberField(TEXT("warmup_frames"), CostWarmupFrameCount);
	Report->SetNumberField(TEXT("measured_frames"), CostMeasuredFrameCount);
	Report->SetNumberField(TEXT("volume_on_median_ms"), VolumeOn.MedianGpuMilliseconds);
	Report->SetNumberField(TEXT("volume_on_mean_ms"), VolumeOn.MeanGpuMilliseconds);
	Report->SetNumberField(TEXT("volume_off_median_ms"), VolumeOff.MedianGpuMilliseconds);
	Report->SetNumberField(TEXT("volume_off_mean_ms"), VolumeOff.MeanGpuMilliseconds);
	Report->SetNumberField(TEXT("volume_cost_median_ms"), VolumeOn.MedianGpuMilliseconds - VolumeOff.MedianGpuMilliseconds);
	Report->SetNumberField(TEXT("drive_cpu_median_ms"), DriveMilliseconds);
	Report->SetNumberField(TEXT("profile_overridden_cvars"), OverriddenWhileDriving);
	Report->SetObjectField(TEXT("profile_while_driving"), ProfileWhileDriving);
	Report->SetObjectField(TEXT("profile_after_release"), ProfileAfterRelease);

	FString ReportText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ReportText);
	FJsonSerializer::Serialize(Report, Writer);
	const FString ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentOps"), FDateTime::Now().ToString(TEXT("%Y%m%d")), TEXT("fx9"), TEXT("hvol_cost.json"));
	TestTrue(TEXT("Wrote the HVOL cost record"), FFileHelper::SaveStringToFile(ReportText, *ReportPath));
	AddInfo(FString::Printf(TEXT("HVOL cost on %.3f ms, off %.3f ms, drive %.3f ms, profile overrides %d -> %s"), VolumeOn.MedianGpuMilliseconds, VolumeOff.MedianGpuMilliseconds, DriveMilliseconds, OverriddenWhileDriving, *ReportPath));
	return true;
}

#endif
