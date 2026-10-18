#if WITH_DEV_AUTOMATION_TESTS

#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/Scene.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace TDFluidVolumeActorPrivate
{
	const TCHAR* const CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const VolumeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	constexpr float StepSeconds = 0.05f;

	struct FTDFluidVolumeActorWorld
	{
		FTDFluidVolumeActorWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidVolumeActorWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidVolumeActorWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		void TickWorld(float TotalSeconds)
		{
			for (float Remaining = TotalSeconds; Remaining > UE_SMALL_NUMBER;)
			{
				const float Delta = FMath::Min(Remaining, StepSeconds);
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Delta);
				Remaining -= Delta;
			}
		}

		UWorld* World = nullptr;
	};

	UTDFluidPresetAsset* MakeSmokePreset(UObject* Outer, float DensityStrength, float PunctureCmPerSec)
	{
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(Outer, ETDFluidPresetKind::Smoke);
		Preset->bOverridePresetDefaults = true;
		Preset->SimulationConfig.GridResolution = 64;
		Preset->SimulationConfig.ActorBrushDensityStrength = DensityStrength;
		Preset->SimulationConfig.ActorBrushPunctureCmPerSec = PunctureCmPerSec;
		return Preset;
	}

	UTDFluidPresetAsset* MakeVolumePreset(UObject* Outer, float HeightCm)
	{
		UTDFluidPresetAsset* Preset = MakeSmokePreset(Outer, -1.0f, 380.0f);
		Preset->VolumeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(VolumeMaterialPath));
		Preset->VolumeMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(CubeMeshPath));
		Preset->VolumeHeightCm = HeightCm;
		return Preset;
	}

	ATDFluidLiveActor* SpawnLiveActor(UWorld* World, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		return World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), Location, Rotation);
	}

	bool WaitUntilVolumeVisible(FTDFluidVolumeActorWorld& TestWorld, const ATDFluidLiveActor* LiveActor)
	{
		for (int32 Attempt = 0; Attempt < 200 && !LiveActor->VolumeMeshComponent->IsVisible(); ++Attempt)
		{
			FlushAsyncLoading();
			TestWorld.TickWorld(StepSeconds);
		}
		return LiveActor->VolumeMeshComponent->IsVisible();
	}

	UMaterialInstanceDynamic* GetVolumeMaterialInstance(const ATDFluidLiveActor* LiveActor)
	{
		return Cast<UMaterialInstanceDynamic>(LiveActor->VolumeMeshComponent->GetMaterial(0));
	}

	float SampleDensityAt(const ATDFluidLiveActor* LiveActor, const FVector2D& WorldLocation)
	{
		FTDFluidCellSample Sample;
		LiveActor->LiveComponent->GetSolver().SampleAtWorldPosition(WorldLocation, Sample);
		return Sample.PrimaryDensity;
	}

	FVector2D SampleVelocityAt(const ATDFluidLiveActor* LiveActor, const FVector2D& WorldLocation)
	{
		FTDFluidCellSample Sample;
		LiveActor->LiveComponent->GetSolver().SampleAtWorldPosition(WorldLocation, Sample);
		return Sample.VelocityCmPerSec;
	}

	int32 StepAndCountStamps(ATDFluidLiveActor* LiveActor)
	{
		LiveActor->StepLiveActor(StepSeconds);
		return LiveActor->LiveComponent->GetLastProcessedPointCount();
	}

	struct FTDFluidBrushOutcome
	{
		float DensityAlongPath = 0.0f;
		float MeanDensityAbovePath = 0.0f;
		float MeanDensityBelowPath = 0.0f;
	};

	float SampleMeanLateralDensity(const ATDFluidLiveActor* LiveActor, float AlongPathCm, float SideSign)
	{
		float DensitySum = 0.0f;
		int32 SampleCount = 0;
		for (float LateralCm = 30.0f; LateralCm <= 105.0f; LateralCm += 15.0f)
		{
			DensitySum += SampleDensityAt(LiveActor, FVector2D(AlongPathCm, SideSign * LateralCm));
			++SampleCount;
		}
		return DensitySum / static_cast<float>(SampleCount);
	}

	FTDFluidBrushOutcome RunPawnCrossingScenario(float DensityStrength, float PunctureCmPerSec)
	{
		FTDFluidVolumeActorWorld TestWorld;
		ATDFluidLiveActor* LiveActor = SpawnLiveActor(TestWorld.World, FVector::ZeroVector);

		UTDFluidPresetAsset* Preset = MakeSmokePreset(TestWorld.World, DensityStrength, PunctureCmPerSec);
		Preset->SimulationConfig.AmbientRefillPerSec = 100.0f;
		LiveActor->InitializeFromPreset(Preset, 0.0f);

		AActor* Crosser = TestWorld.World->SpawnActor<AActor>();
		USceneComponent* CrosserRoot = NewObject<USceneComponent>(Crosser);
		Crosser->AddInstanceComponent(CrosserRoot);
		Crosser->SetRootComponent(CrosserRoot);
		CrosserRoot->RegisterComponent();
		Crosser->SetActorLocation(FVector(-120.0f, 0.0f, 0.0f));
		LiveActor->RegisterInteractingActor(Crosser);

		LiveActor->StepLiveActor(StepSeconds);

		FTDFluidSimulationConfig DrainedConfig = LiveActor->LiveComponent->SimulationConfig;
		DrainedConfig.AmbientRefillPerSec = 0.0f;
		LiveActor->LiveComponent->ApplySimulationConfig(DrainedConfig);

		for (int32 Step = 1; Step <= 8; ++Step)
		{
			Crosser->SetActorLocation(FVector(-120.0f + 30.0f * Step, 0.0f, 0.0f));
			LiveActor->StepLiveActor(StepSeconds);
		}

		FTDFluidBrushOutcome Outcome;
		Outcome.DensityAlongPath = SampleDensityAt(LiveActor, FVector2D(-30.0f, 0.0f));
		Outcome.MeanDensityAbovePath = SampleMeanLateralDensity(LiveActor, 60.0f, 1.0f);
		Outcome.MeanDensityBelowPath = SampleMeanLateralDensity(LiveActor, 60.0f, -1.0f);
		return Outcome;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeBoxFitsSimulationTest,
	"TDGame.FluidFX.VolumeActor.BoxFitsSimulationRangeAndStaysWorldAligned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeBoxFitsSimulationTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumeActorPrivate;

	FTDFluidVolumeActorWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnLiveActor(TestWorld.World, FVector(1000.0f, 500.0f, 40.0f), FRotator(0.0f, 45.0f, 0.0f));
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor) || !TestNotNull(TEXT("Live actor owns a volume mesh component"), LiveActor->VolumeMeshComponent.Get()))
	{
		return false;
	}

	UStaticMeshComponent* VolumeMesh = LiveActor->VolumeMeshComponent;
	TestFalse(TEXT("Volume mesh starts hidden"), VolumeMesh->IsVisible());
	TestTrue(TEXT("Volume mesh has no mesh before a preset arrives"), VolumeMesh->GetStaticMesh() == nullptr);
	TestEqual(TEXT("Volume mesh has no collision"), VolumeMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestEqual(TEXT("Volume mesh ignores the visibility channel"), VolumeMesh->GetCollisionResponseToChannel(ECC_Visibility), ECR_Ignore);
	TestFalse(TEXT("Volume mesh casts no shadow"), VolumeMesh->CastShadow);

	UTDFluidPresetAsset* Preset = MakeVolumePreset(TestWorld.World, 180.0f);
	LiveActor->InitializeFromPreset(Preset, 0.0f);
	if (!TestTrue(TEXT("Volume mesh becomes visible after the async load"), WaitUntilVolumeVisible(TestWorld, LiveActor)))
	{
		return false;
	}

	TestTrue(TEXT("Preset volume mesh is assigned"), VolumeMesh->GetStaticMesh() != nullptr);
	TestTrue(TEXT("Volume mesh uses a dynamic material instance"), GetVolumeMaterialInstance(LiveActor) != nullptr);
	TestTrue(TEXT("Volume mesh ignores actor rotation"), VolumeMesh->GetComponentQuat().Equals(FQuat::Identity, 1.0e-4));

	FBox Box = VolumeMesh->Bounds.GetBox();
	TestTrue(TEXT("Box minimum matches range center, actor floor"), Box.Min.Equals(FVector(650.0f, 150.0f, 40.0f), 0.5f));
	TestTrue(TEXT("Box maximum matches range corner, floor plus height"), Box.Max.Equals(FVector(1350.0f, 850.0f, 220.0f), 0.5f));

	FHitResult VisibilityHit;
	const bool bHitVolume = TestWorld.World->LineTraceSingleByChannel(VisibilityHit, FVector(1000.0f, 500.0f, 1000.0f), FVector(1000.0f, 500.0f, -500.0f), ECC_Visibility);
	TestFalse(TEXT("Visibility trace passes through the volume box"), bHitVolume);

	Preset->VolumeHeightCm = 260.0f;
	LiveActor->SetActorLocation(FVector(1000.0f, 500.0f, 100.0f));
	TestWorld.TickWorld(StepSeconds);
	Box = VolumeMesh->Bounds.GetBox();
	TestTrue(TEXT("Box follows actor floor and new height"), Box.Min.Equals(FVector(650.0f, 150.0f, 100.0f), 0.5f) && Box.Max.Equals(FVector(1350.0f, 850.0f, 360.0f), 0.5f));

	FTDFluidSimulationConfig WideConfig = LiveActor->LiveComponent->SimulationConfig;
	WideConfig.SimExtentCm = FVector2D(1000.0f, 500.0f);
	LiveActor->LiveComponent->ApplySimulationConfig(WideConfig);
	TestWorld.TickWorld(StepSeconds);
	Box = VolumeMesh->Bounds.GetBox();
	TestTrue(TEXT("Box follows the solver range"), Box.Min.Equals(FVector(500.0f, 250.0f, 100.0f), 0.5f) && Box.Max.Equals(FVector(1500.0f, 750.0f, 360.0f), 0.5f));
	TestTrue(TEXT("Box is still world aligned after refit"), VolumeMesh->GetComponentQuat().Equals(FQuat::Identity, 1.0e-4));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeMaterialParametersTest,
	"TDGame.FluidFX.VolumeActor.MaterialInstanceReceivesSimulationParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeMaterialParametersTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumeActorPrivate;

	FTDFluidVolumeActorWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnLiveActor(TestWorld.World, FVector(1000.0f, 500.0f, 40.0f));
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor))
	{
		return false;
	}

	UTDFluidPresetAsset* Preset = MakeVolumePreset(TestWorld.World, 180.0f);
	LiveActor->InitializeFromPreset(Preset, 0.0f);
	if (!TestTrue(TEXT("Volume mesh becomes visible after the async load"), WaitUntilVolumeVisible(TestWorld, LiveActor)))
	{
		return false;
	}

	UMaterialInstanceDynamic* VolumeMaterial = GetVolumeMaterialInstance(LiveActor);
	if (!TestNotNull(TEXT("Volume material instance exists"), VolumeMaterial))
	{
		return false;
	}

	TestWorld.TickWorld(StepSeconds);
	const FLinearColor Center = VolumeMaterial->K2_GetVectorParameterValue(TEXT("SimCenterWorld"));
	const FLinearColor Extents = VolumeMaterial->K2_GetVectorParameterValue(TEXT("SimExtentsCm"));
	TestNearlyEqual(TEXT("SimCenterWorld x"), Center.R, 1000.0f, 0.01f);
	TestNearlyEqual(TEXT("SimCenterWorld y"), Center.G, 500.0f, 0.01f);
	TestNearlyEqual(TEXT("SimCenterWorld z is the actor floor"), Center.B, 40.0f, 0.01f);
	TestNearlyEqual(TEXT("SimExtentsCm x"), Extents.R, 700.0f, 0.01f);
	TestNearlyEqual(TEXT("SimExtentsCm y"), Extents.G, 700.0f, 0.01f);
	TestNearlyEqual(TEXT("VolumeHeightCm"), VolumeMaterial->K2_GetScalarParameterValue(TEXT("VolumeHeightCm")), 180.0f, 0.01f);

	Preset->VolumeHeightCm = 260.0f;
	LiveActor->SetActorLocation(FVector(1000.0f, 500.0f, 100.0f));
	TestWorld.TickWorld(StepSeconds);
	TestNearlyEqual(TEXT("VolumeHeightCm follows the preset"), VolumeMaterial->K2_GetScalarParameterValue(TEXT("VolumeHeightCm")), 260.0f, 0.01f);
	TestNearlyEqual(TEXT("SimCenterWorld z follows the actor"), VolumeMaterial->K2_GetVectorParameterValue(TEXT("SimCenterWorld")).B, 100.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeLightTrackingTest,
	"TDGame.FluidFX.VolumeActor.LightActorDrivesLightParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeLightTrackingTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumeActorPrivate;

	FTDFluidVolumeActorWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnLiveActor(TestWorld.World, FVector(1000.0f, 500.0f, 40.0f));
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor))
	{
		return false;
	}

	LiveActor->InitializeFromPreset(MakeVolumePreset(TestWorld.World, 180.0f), 0.0f);
	if (!TestTrue(TEXT("Volume mesh becomes visible after the async load"), WaitUntilVolumeVisible(TestWorld, LiveActor)))
	{
		return false;
	}

	UMaterialInstanceDynamic* VolumeMaterial = GetVolumeMaterialInstance(LiveActor);
	if (!TestNotNull(TEXT("Volume material instance exists"), VolumeMaterial))
	{
		return false;
	}

	TestWorld.TickWorld(StepSeconds);
	TestNearlyEqual(TEXT("Light mode is 0 without a light actor"), VolumeMaterial->K2_GetScalarParameterValue(TEXT("VolumeLightMode")), 0.0f, 0.01f);

	APointLight* PointLightActor = TestWorld.World->SpawnActor<APointLight>(APointLight::StaticClass(), FVector(1200.0f, 400.0f, 300.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Spawned point light"), PointLightActor))
	{
		return false;
	}
	UPointLightComponent* PointLight = PointLightActor->PointLightComponent;
	PointLight->IntensityUnits = ELightUnits::Unitless;
	PointLight->Intensity = 800.0f;
	PointLight->LightColor = FColor(255, 128, 64, 255);
	PointLight->AttenuationRadius = 650.0f;

	LiveActor->VolumeLightActor = PointLightActor;
	TestWorld.TickWorld(StepSeconds);

	const FLinearColor ExpectedColor = PointLight->GetLightColor() * 0.8f;
	const FLinearColor PointColor = VolumeMaterial->K2_GetVectorParameterValue(TEXT("VolumeLightColor"));
	const FLinearColor PointPosition = VolumeMaterial->K2_GetVectorParameterValue(TEXT("VolumeLightPosition"));
	TestNearlyEqual(TEXT("Point light mode"), VolumeMaterial->K2_GetScalarParameterValue(TEXT("VolumeLightMode")), 1.0f, 0.01f);
	TestNearlyEqual(TEXT("Point light radius is the attenuation radius"), VolumeMaterial->K2_GetScalarParameterValue(TEXT("VolumeLightRadius")), 650.0f, 0.01f);
	TestNearlyEqual(TEXT("Point light position x"), PointPosition.R, 1200.0f, 0.01f);
	TestNearlyEqual(TEXT("Point light position y"), PointPosition.G, 400.0f, 0.01f);
	TestNearlyEqual(TEXT("Point light position z"), PointPosition.B, 300.0f, 0.01f);
	TestNearlyEqual(TEXT("Point light color red is normalized color times intensity"), PointColor.R, ExpectedColor.R, 1.0e-4f);
	TestNearlyEqual(TEXT("Point light color green"), PointColor.G, ExpectedColor.G, 1.0e-4f);
	TestNearlyEqual(TEXT("Point light color blue"), PointColor.B, ExpectedColor.B, 1.0e-4f);

	PointLight->Intensity = 1600.0f;
	TestWorld.TickWorld(StepSeconds);
	TestNearlyEqual(TEXT("Doubling intensity doubles the color"), VolumeMaterial->K2_GetVectorParameterValue(TEXT("VolumeLightColor")).R, ExpectedColor.R * 2.0f, 1.0e-4f);

	PointLight->IntensityUnits = ELightUnits::Candelas;
	PointLight->Intensity = 8.0f;
	TestWorld.TickWorld(StepSeconds);
	TestNearlyEqual(TEXT("Candelas 8 resolves to the same color as Unitless 800"), VolumeMaterial->K2_GetVectorParameterValue(TEXT("VolumeLightColor")).R, ExpectedColor.R, 1.0e-4f);

	PointLight->IntensityUnits = ELightUnits::Lumens;
	PointLight->Intensity = 100.5f;
	TestWorld.TickWorld(StepSeconds);
	TestNearlyEqual(TEXT("Lumens 100.5 resolves to about the Candelas 8 color"), VolumeMaterial->K2_GetVectorParameterValue(TEXT("VolumeLightColor")).R, ExpectedColor.R, 1.0e-3f);

	PointLight->IntensityUnits = ELightUnits::EV;
	PointLight->Intensity = 3.0f;
	TestWorld.TickWorld(StepSeconds);
	TestNearlyEqual(TEXT("EV 3 resolves to the Candelas 8 color"), VolumeMaterial->K2_GetVectorParameterValue(TEXT("VolumeLightColor")).R, ExpectedColor.R, 1.0e-4f);

	PointLight->IntensityUnits = ELightUnits::Unitless;
	PointLight->Intensity = 100000.0f;
	TestWorld.TickWorld(StepSeconds);
	TestNearlyEqual(TEXT("Point light color intensity is clamped to 8"), VolumeMaterial->K2_GetVectorParameterValue(TEXT("VolumeLightColor")).R, PointLight->GetLightColor().R * 8.0f, 1.0e-3f);

	const FRotator DirectionalRotation(-40.0f, 35.0f, 0.0f);
	ADirectionalLight* DirectionalLightActor = TestWorld.World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FVector::ZeroVector, DirectionalRotation);
	if (!TestNotNull(TEXT("Spawned directional light"), DirectionalLightActor))
	{
		return false;
	}
	UDirectionalLightComponent* DirectionalLight = Cast<UDirectionalLightComponent>(DirectionalLightActor->GetLightComponent());
	if (!TestNotNull(TEXT("Directional light component"), DirectionalLight))
	{
		return false;
	}
	DirectionalLight->Intensity = 10.0f;

	LiveActor->VolumeLightActor = DirectionalLightActor;
	TestWorld.TickWorld(StepSeconds);
	const FLinearColor DirectionalColor = VolumeMaterial->K2_GetVectorParameterValue(TEXT("VolumeLightColor"));
	const FLinearColor DirectionalVector = VolumeMaterial->K2_GetVectorParameterValue(TEXT("VolumeLightDirection"));
	const FVector ExpectedDirection = DirectionalLight->GetForwardVector();
	TestNearlyEqual(TEXT("Directional light mode"), VolumeMaterial->K2_GetScalarParameterValue(TEXT("VolumeLightMode")), 2.0f, 0.01f);
	TestNearlyEqual(TEXT("Directional light color uses the lux reference"), DirectionalColor.R, DirectionalLight->GetLightColor().R, 1.0e-4f);
	TestNearlyEqual(TEXT("Directional light direction x"), DirectionalVector.R, static_cast<float>(ExpectedDirection.X), 1.0e-3f);
	TestNearlyEqual(TEXT("Directional light direction y"), DirectionalVector.G, static_cast<float>(ExpectedDirection.Y), 1.0e-3f);
	TestNearlyEqual(TEXT("Directional light direction z"), DirectionalVector.B, static_cast<float>(ExpectedDirection.Z), 1.0e-3f);

	DirectionalLight->Intensity = 100000.0f;
	TestWorld.TickWorld(StepSeconds);
	TestNearlyEqual(TEXT("Directional light 100000 lux clamps the color intensity to 8"), VolumeMaterial->K2_GetVectorParameterValue(TEXT("VolumeLightColor")).R, DirectionalLight->GetLightColor().R * 8.0f, 1.0e-3f);

	LiveActor->VolumeLightActor = nullptr;
	TestWorld.TickWorld(StepSeconds);
	TestNearlyEqual(TEXT("Light mode returns to 0 when the light actor is cleared"), VolumeMaterial->K2_GetScalarParameterValue(TEXT("VolumeLightMode")), 0.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeHiddenWithoutVolumeMaterialTest,
	"TDGame.FluidFX.VolumeActor.PresetWithoutVolumeHidesTheBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeHiddenWithoutVolumeMaterialTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumeActorPrivate;

	FTDFluidVolumeActorWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnLiveActor(TestWorld.World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor))
	{
		return false;
	}

	LiveActor->InitializeFromPreset(MakeVolumePreset(TestWorld.World, 150.0f), 0.0f);
	if (!TestTrue(TEXT("Volume mesh becomes visible after the async load"), WaitUntilVolumeVisible(TestWorld, LiveActor)))
	{
		return false;
	}

	LiveActor->InitializeFromPreset(UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Water), 0.0f);
	TestFalse(TEXT("Volume mesh hides when the new preset has no volume material"), LiveActor->VolumeMeshComponent->IsVisible());
	TestTrue(TEXT("Volume material instance is released"), GetVolumeMaterialInstance(LiveActor) == nullptr);

	LiveActor->InitializeFromPreset(MakeVolumePreset(TestWorld.World, 150.0f), 0.0f);
	TestTrue(TEXT("Volume mesh shows again for a preset with a volume material"), WaitUntilVolumeVisible(TestWorld, LiveActor));
	TestTrue(TEXT("Volume material instance is created again"), GetVolumeMaterialInstance(LiveActor) != nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidActorBrushSettingsTest,
	"TDGame.FluidFX.VolumeActor.ActorBrushUsesEraseAndPunctureSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidActorBrushSettingsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumeActorPrivate;

	const FTDFluidBrushOutcome DefaultOutcome = RunPawnCrossingScenario(1.0f, 0.0f);
	const FTDFluidBrushOutcome EraseOutcome = RunPawnCrossingScenario(-1.0f, 0.0f);
	const FTDFluidBrushOutcome PunctureOutcome = RunPawnCrossingScenario(-1.0f, 380.0f);

	TestTrue(TEXT("Default actor brush keeps the ambient density along the path"), DefaultOutcome.DensityAlongPath > 0.7f);
	TestTrue(TEXT("Negative actor brush strength erases density along the path"), EraseOutcome.DensityAlongPath < DefaultOutcome.DensityAlongPath - 0.1f);

	const float AboveShift = PunctureOutcome.MeanDensityAbovePath - EraseOutcome.MeanDensityAbovePath;
	const float BelowShift = PunctureOutcome.MeanDensityBelowPath - EraseOutcome.MeanDensityBelowPath;
	AddInfo(FString::Printf(TEXT("puncture lateral density: erase above=%.4f below=%.4f | puncture above=%.4f below=%.4f"), EraseOutcome.MeanDensityAbovePath, EraseOutcome.MeanDensityBelowPath, PunctureOutcome.MeanDensityAbovePath, PunctureOutcome.MeanDensityBelowPath));
	TestTrue(*FString::Printf(TEXT("Puncture widens the erased path on both sides (%.4f, %.4f)"), AboveShift, BelowShift), AboveShift < -0.003f && BelowShift < -0.003f);
	TestTrue(*FString::Printf(TEXT("Puncture widens the erased path symmetrically about the path (%.4f, %.4f)"), AboveShift, BelowShift), FMath::Abs(AboveShift - BelowShift) <= 0.5f * FMath::Max(FMath::Abs(AboveShift), FMath::Abs(BelowShift)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidMouseBrushStampTest,
	"TDGame.FluidFX.VolumeActor.MouseBrushStampsFromCursorMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidMouseBrushStampTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumeActorPrivate;

	FTDFluidVolumeActorWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnLiveActor(TestWorld.World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor))
	{
		return false;
	}

	TestFalse(TEXT("Mouse brush is off by default"), LiveActor->bAcceptMouseBrush);
	TestNearlyEqual(TEXT("Default mouse brush radius"), LiveActor->MouseBrushRadiusCm, 120.0f, 0.001f);

	UTDFluidPresetAsset* WaterPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Water);
	WaterPreset->bOverridePresetDefaults = true;
	WaterPreset->SimulationConfig.SimExtentCm = FVector2D(2000.0f, 2000.0f);
	LiveActor->InitializeFromPreset(WaterPreset, 0.0f);
	LiveActor->bAcceptMouseBrush = true;
	LiveActor->MouseBrushRadiusCm = 90.0f;

	TestEqual(TEXT("Polling without a player controller stamps nothing"), StepAndCountStamps(LiveActor), 0);

	LiveActor->QueueMouseBrushAt(FVector(0.0f, 0.0f, 50.0f), StepSeconds);
	TestEqual(TEXT("First cursor sample only records the position"), StepAndCountStamps(LiveActor), 0);

	LiveActor->QueueMouseBrushAt(FVector(0.0f, 0.0f, 50.0f), StepSeconds);
	LiveActor->QueueMouseBrushAt(FVector(60.0f, 0.0f, 50.0f), StepSeconds);
	TestEqual(TEXT("Cursor movement queues one stamp"), StepAndCountStamps(LiveActor), 1);
	TestTrue(TEXT("Stamp paints density at the cursor"), SampleDensityAt(LiveActor, FVector2D(60.0f, 0.0f)) > 0.25f);
	TestNearlyEqual(TEXT("Stamp stays inside the brush radius"), SampleDensityAt(LiveActor, FVector2D(300.0f, 0.0f)), 0.0f, 1.0e-3f);
	TestTrue(TEXT("Stamp carries the cursor velocity"), SampleVelocityAt(LiveActor, FVector2D(60.0f, 0.0f)).X > 100.0f);

	LiveActor->QueueMouseBrushAt(FVector(120.0f, 0.0f, 50.0f), StepSeconds);
	TestEqual(TEXT("A frame without a held button ends the stroke"), StepAndCountStamps(LiveActor), 0);

	LiveActor->QueueMouseBrushAt(FVector(100.0f, 0.0f, 50.0f), StepSeconds);
	LiveActor->QueueMouseBrushAt(FVector(900.0f, 0.0f, 50.0f), StepSeconds);
	TestEqual(TEXT("Cursor jump above the teleport speed is ignored"), StepAndCountStamps(LiveActor), 0);

	LiveActor->QueueMouseBrushAt(FVector(100.0f, 0.0f, 50.0f), StepSeconds);
	LiveActor->QueueMouseBrushAt(FVector(900.0f, 0.0f, 50.0f), StepSeconds);
	LiveActor->QueueMouseBrushAt(FVector(960.0f, 0.0f, 50.0f), StepSeconds);
	TestEqual(TEXT("Stroke continues from the jumped position"), StepAndCountStamps(LiveActor), 1);

	LiveActor->QueueMouseBrushAt(FVector(100.0f, 0.0f, 50.0f), StepSeconds);
	LiveActor->QueueMouseBrushAt(FVector(100.2f, 0.0f, 50.0f), StepSeconds);
	TestEqual(TEXT("Cursor movement below the minimum speed is ignored"), StepAndCountStamps(LiveActor), 0);

	LiveActor->QueueMouseBrushAt(FVector(100.0f, 0.0f, 50.0f), StepSeconds);
	LiveActor->QueueMouseBrushAt(FVector(5000.0f, 0.0f, 0.0f), StepSeconds);
	LiveActor->QueueMouseBrushAt(FVector(160.0f, 0.0f, 50.0f), StepSeconds);
	TestEqual(TEXT("Leaving the simulation range ends the stroke"), StepAndCountStamps(LiveActor), 0);

	LiveActor->QueueMouseBrushAt(FVector(100.0f, 0.0f, 50.0f), StepSeconds);
	LiveActor->QueueMouseBrushAt(FVector(160.0f, 0.0f, 50.0f), StepSeconds);
	TestEqual(TEXT("Second sample of a new stroke stamps"), StepAndCountStamps(LiveActor), 1);

	return true;
}

#endif
