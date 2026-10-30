#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidUnderwaterSubsystem.h"
#include "FluidFX/TDFluidVolumeDriverComponent.h"
#include "FluidFX/TDFluidVolumeDriverTypes.h"
#include "FluidFX/TDFluidWaterPropActor.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace TDFluidWaterPropRhiTestPrivate
{
	constexpr float FrameSeconds = 1.0f / 60.0f;
	constexpr int32 SlotWarmupFrameCount = 30;
	constexpr int32 CostWarmupFrameCount = 120;
	constexpr int32 CostMeasuredFrameCount = 240;
	const TCHAR* const SphereClearPresetPath = TEXT("/Game/FluidFX/Presets/DA_TDFluid_LiquidSphereClear.DA_TDFluid_LiquidSphereClear");
	const TCHAR* const SphereClearMaterialPath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidLiquidSphere_Clear.MI_TD_FluidLiquidSphere_Clear");
	const TCHAR* const UnderwaterTankMaterialPath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidUnderwater_Tank.MI_TD_FluidUnderwater_Tank");
	const TCHAR* const SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");

	bool CanRunWaterPropRhiTest(FAutomationTestBase& Test, const FString& CaseLabel)
	{
		if (!GUsingNullRHI && FApp::CanEverRender())
		{
			return true;
		}
		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), *CaseLabel));
		return false;
	}

	struct FTDFluidWaterPropRhiTestWorld
	{
		FTDFluidWaterPropRhiTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidWaterPropRhiTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidWaterPropRhiTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		void TickWorld(int32 FrameCount)
		{
			for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, FrameSeconds);
			}
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	TextureAddress ReadWaterPropTextureAddressX(const UTexture& Texture)
	{
		if (const UTexture2D* Texture2D = Cast<UTexture2D>(&Texture))
		{
			return Texture2D->AddressX;
		}
		if (const UTextureRenderTarget2D* RenderTarget = Cast<UTextureRenderTarget2D>(&Texture))
		{
			return RenderTarget->AddressX;
		}
		return TA_MAX;
	}

	double ReadWaterPropPercentile(TArray<double> Samples, double Fraction)
	{
		if (Samples.IsEmpty())
		{
			return 0.0;
		}

		Samples.Sort();
		const int32 SampleIndex = FMath::Clamp(FMath::CeilToInt32(Fraction * Samples.Num()) - 1, 0, Samples.Num() - 1);
		return Samples[SampleIndex];
	}

	ATDFluidLiveActor* SpawnWaterPropCostActor(UWorld& World, ETDFluidPresetKind Kind, const FVector& Location)
	{
		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(&World, Kind);
		ATDFluidLiveActor* LiveActor = Kind == ETDFluidPresetKind::Waterfall
			? World.SpawnActor<ATDFluidWaterPropActor>(ATDFluidWaterPropActor::StaticClass(), Location, FRotator::ZeroRotator)
			: World.SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!Preset || !LiveActor)
		{
			return nullptr;
		}

		LiveActor->InitializeFromPreset(Preset, 0.0f);
		return LiveActor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterPropRhiLiquidSphereSlotDrivenTest,
	"TDGame.FluidFXRhi.WaterProps.LiquidSphereSlotDriven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterPropRhiLiquidSphereSlotDrivenTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterPropRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.WaterProps.LiquidSphereSlotDriven");
	if (!CanRunWaterPropRhiTest(*this, CaseLabel))
	{
		return false;
	}

	UTDFluidPresetAsset* Preset = LoadObject<UTDFluidPresetAsset>(nullptr, SphereClearPresetPath);
	UMaterialInterface* ExpectedParent = LoadObject<UMaterialInterface>(nullptr, SphereClearMaterialPath);
	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, SphereMeshPath);
	if (!TestNotNull(TEXT("stage preset DA_TDFluid_LiquidSphereClear (editor_make_fluid_water_props_stage.py)"), Preset)
		|| !TestNotNull(TEXT("sphere material MI_TD_FluidLiquidSphere_Clear (editor_make_fluid_water_props_material.py)"), ExpectedParent)
		|| !TestNotNull(TEXT("engine sphere mesh"), SphereMesh))
	{
		return false;
	}
	for (const TSoftObjectPtr<UMaterialInterface>& SlotMaterial : Preset->PrimaryOutputMaterials)
	{
		SlotMaterial.LoadSynchronous();
	}
	Preset->ResolveSlotMaterial(ETDFluidOutputMaterialSlot::Primary, 0).LoadSynchronous();
	if (!TestTrue(TEXT("preset names a primary output tag"), Preset->PrimaryOutputActorTag != NAME_None))
	{
		return false;
	}

	FTDFluidWaterPropRhiTestWorld TestWorld;
	AStaticMeshActor* SphereActor = TestWorld.World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator);
	ATDFluidLiveActor* LiveActor = TestWorld.World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), FVector(0.0, 0.0, -5000.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("sphere actor"), SphereActor) || !TestNotNull(TEXT("live actor"), LiveActor))
	{
		return false;
	}

	UStaticMeshComponent* SphereComponent = SphereActor->GetStaticMeshComponent();
	SphereComponent->SetMobility(EComponentMobility::Movable);
	SphereComponent->SetStaticMesh(SphereMesh);
	SphereActor->SetActorScale3D(FVector(2.0));
	SphereActor->Tags.Add(Preset->PrimaryOutputActorTag);
	LiveActor->InitializeFromPreset(Preset, 0.0f);
	TestWorld.TickWorld(SlotWarmupFrameCount);
	if (!TestNotNull(TEXT("tagged slot needs a volume driver"), ToRawPtr(LiveActor->VolumeDriver)))
	{
		return false;
	}
	LiveActor->VolumeDriver->CollectTaggedTargets(TestWorld.World->GetTimeSeconds(), true);
	TestWorld.TickWorld(2);

	UMaterialInstanceDynamic* SlotMaterialInstance = Cast<UMaterialInstanceDynamic>(SphereComponent->GetMaterial(0));
	if (!TestNotNull(TEXT("tagged sphere uses a slot MID"), SlotMaterialInstance))
	{
		return false;
	}
	TestTrue(TEXT("slot MID parent is MI_TD_FluidLiquidSphere_Clear"), SlotMaterialInstance->Parent == ExpectedParent);

	UTexture* SourceOutput = LiveActor->LiveComponent ? LiveActor->LiveComponent->GetDensityPressureTexture() : nullptr;
	if (!TestNotNull(TEXT("live density output"), SourceOutput))
	{
		return false;
	}
	TestTrue(TEXT("slot MID samples the live density output"), SlotMaterialInstance->K2_GetTextureParameterValue(FName(TEXT("DensityPressureTexture"))) == SourceOutput);
	TestEqual(TEXT("density output wraps"), static_cast<int32>(ReadWaterPropTextureAddressX(*SourceOutput)), static_cast<int32>(TA_Wrap));
	TestEqual(TEXT("slot MID marks the area wrapped"), SlotMaterialInstance->K2_GetScalarParameterValue(FName(TEXT("SimAreaWrapped"))), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterPropRhiUnderwaterBoxSurfaceZTest,
	"TDGame.FluidFXRhi.WaterProps.UnderwaterBoxSurfaceZ",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterPropRhiUnderwaterBoxSurfaceZTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterPropRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.WaterProps.UnderwaterBoxSurfaceZ");
	if (!CanRunWaterPropRhiTest(*this, CaseLabel))
	{
		return false;
	}

	UMaterialInterface* TankMaterial = LoadObject<UMaterialInterface>(nullptr, UnderwaterTankMaterialPath);
	if (!TestNotNull(TEXT("underwater material MI_TD_FluidUnderwater_Tank (editor_make_fluid_water_props_material.py)"), TankMaterial))
	{
		return false;
	}

	FTDFluidWaterPropRhiTestWorld TestWorld;
	UTDFluidUnderwaterSubsystem* Subsystem = TestWorld.World->GetSubsystem<UTDFluidUnderwaterSubsystem>();
	APostProcessVolume* Volume = TestWorld.World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("underwater subsystem"), Subsystem) || !TestNotNull(TEXT("bounded post process volume"), Volume))
	{
		return false;
	}

	Volume->bUnbound = false;
	Volume->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, TankMaterial));
	Volume->Tags.Add(UTDFluidUnderwaterSubsystem::UnderwaterVolumeTag);
	Subsystem->NotifyUnderwaterVolumesChanged();

	UMaterialInstanceDynamic* DrivenMaterial = Cast<UMaterialInstanceDynamic>(ToRawPtr(Volume->Settings.WeightedBlendables.Array[0].Object));
	if (!TestNotNull(TEXT("tank blendable became a MID"), DrivenMaterial))
	{
		return false;
	}
	TestTrue(TEXT("MID parent is MI_TD_FluidUnderwater_Tank"), DrivenMaterial->Parent == TankMaterial);
	const float BoxTopCm = static_cast<float>(Volume->GetComponentsBoundingBox(true).Max.Z);
	TestEqual(TEXT("UnderwaterSurfaceZCm is the box top"), DrivenMaterial->K2_GetScalarParameterValue(UTDFluidUnderwaterSubsystem::SurfaceHeightParameterName), BoxTopCm, 1.0e-3f);
	AddInfo(FString::Printf(TEXT("%s: box top %.2f cm"), *CaseLabel, BoxTopCm));

	Subsystem->RestoreDrivenVolumesForTests();
	TestTrue(TEXT("restore puts the tank material back"), ToRawPtr(Volume->Settings.WeightedBlendables.Array[0].Object) == TankMaterial);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidWaterPropRhiOffscreenCostTest,
	"TDGame.FluidFXRhi.WaterProps.OffscreenCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidWaterPropRhiOffscreenCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidWaterPropRhiTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.WaterProps.OffscreenCost");
	if (!CanRunWaterPropRhiTest(*this, CaseLabel))
	{
		return false;
	}

	FTDFluidWaterPropRhiTestWorld TestWorld;
	const ETDFluidPresetKind CostKinds[] = {
		ETDFluidPresetKind::LiquidSphereClear,
		ETDFluidPresetKind::LiquidSphereTurbulent,
		ETDFluidPresetKind::BloodOrb,
		ETDFluidPresetKind::Waterfall,
		ETDFluidPresetKind::WaterPool
	};
	const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ActorEntries;
	for (int32 KindIndex = 0; KindIndex < static_cast<int32>(UE_ARRAY_COUNT(CostKinds)); ++KindIndex)
	{
		ATDFluidLiveActor* LiveActor = SpawnWaterPropCostActor(*TestWorld.World, CostKinds[KindIndex], FVector(3000.0 * KindIndex, 0.0, 300.0));
		if (!TestNotNull(*FString::Printf(TEXT("cost actor %d"), KindIndex), LiveActor))
		{
			return false;
		}

		const TSharedRef<FJsonObject> ActorEntry = MakeShared<FJsonObject>();
		ActorEntry->SetStringField(TEXT("kind"), StaticEnum<ETDFluidPresetKind>()->GetNameStringByValue(static_cast<int64>(CostKinds[KindIndex])));
		ActorEntry->SetBoolField(TEXT("gpu"), LiveActor->LiveComponent && LiveActor->LiveComponent->IsUsingGpuSolver());
		ActorEntry->SetNumberField(TEXT("resolution"), LiveActor->LiveComponent ? LiveActor->LiveComponent->GetSimulationResolution() : 0);
		ActorEntries.Add(MakeShared<FJsonValueObject>(ActorEntry));
	}

	TArray<double> GameThreadMilliseconds;
	TArray<double> GpuWaitMilliseconds;
	for (int32 FrameIndex = 0; FrameIndex < CostWarmupFrameCount + CostMeasuredFrameCount; ++FrameIndex)
	{
		const double TickStartSeconds = FPlatformTime::Seconds();
		++GFrameCounter;
		TestWorld.World->Tick(LEVELTICK_All, FrameSeconds);
		const double TickEndSeconds = FPlatformTime::Seconds();
		ENQUEUE_RENDER_COMMAND(TDFluidWaterPropCostWait)([](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.SubmitAndBlockUntilGPUIdle();
		});
		FlushRenderingCommands();
		if (FrameIndex >= CostWarmupFrameCount)
		{
			GameThreadMilliseconds.Add((TickEndSeconds - TickStartSeconds) * 1000.0);
			GpuWaitMilliseconds.Add((FPlatformTime::Seconds() - TickEndSeconds) * 1000.0);
		}
	}

	const double GameThreadMedian = ReadWaterPropPercentile(GameThreadMilliseconds, 0.5);
	const double GameThreadP95 = ReadWaterPropPercentile(GameThreadMilliseconds, 0.95);
	const double GpuWaitMedian = ReadWaterPropPercentile(GpuWaitMilliseconds, 0.5);
	const double GpuWaitP95 = ReadWaterPropPercentile(GpuWaitMilliseconds, 0.95);
	Report->SetStringField(TEXT("test"), CaseLabel);
	Report->SetArrayField(TEXT("actors"), ActorEntries);
	Report->SetNumberField(TEXT("warmup_frames"), CostWarmupFrameCount);
	Report->SetNumberField(TEXT("measured_frames"), GameThreadMilliseconds.Num());
	Report->SetNumberField(TEXT("game_thread_median_ms"), GameThreadMedian);
	Report->SetNumberField(TEXT("game_thread_p95_ms"), GameThreadP95);
	Report->SetNumberField(TEXT("gpu_wait_median_ms"), GpuWaitMedian);
	Report->SetNumberField(TEXT("gpu_wait_p95_ms"), GpuWaitP95);
	FString ReportText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ReportText);
	FJsonSerializer::Serialize(Report, Writer);
	const FString CostPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentOps"), TEXT("20261029"), TEXT("wave3"), TEXT("w5-props-cost.json"));
	TestTrue(TEXT("wrote the water prop cost report"), FFileHelper::SaveStringToFile(ReportText, *CostPath));
	AddInfo(FString::Printf(TEXT("%s: game thread median %.4f ms p95 %.4f ms, gpu wait median %.4f ms p95 %.4f ms -> %s"), *CaseLabel, GameThreadMedian, GameThreadP95, GpuWaitMedian, GpuWaitP95, *CostPath));
	TestFalse(TEXT("game thread cost is a number"), FMath::IsNaN(GameThreadMedian));
	TestFalse(TEXT("gpu wait cost is a number"), FMath::IsNaN(GpuWaitMedian));
	return true;
}

#endif
