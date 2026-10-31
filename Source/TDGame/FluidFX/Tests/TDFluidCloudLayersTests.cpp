#if WITH_DEV_AUTOMATION_TESTS

#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidCloudLayersActor.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tickable.h"
#include "UObject/Package.h"

namespace TDFluidCloudLayersTestPrivate
{
	const TCHAR* const TestSlotsMaterialPath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidCloud_TestSlots.MI_TD_FluidCloud_TestSlots");
	const TCHAR* const SkyOnlyMaterialPath = TEXT("/Game/FluidFX/Materials/MI_TD_FluidCloud_SkyOnly.MI_TD_FluidCloud_SkyOnly");
	const TCHAR* const SetupConsoleName = TEXT("TD.FluidFX.CloudSetup");
	const TCHAR* const ViewScaleConsoleName = TEXT("TD.FluidFX.CloudViewSampleScale");
	const FName SourceTagA(TEXT("TDFluidCloud_TestA"));
	const FName SourceTagB(TEXT("TDFluidCloud_TestB"));
	const FName CloudFlowParameterName(TEXT("CloudFlowSeconds"));
	constexpr float StepSeconds = 1.0f / 60.0f;
	constexpr double FlowToleranceSeconds = 1e-3;
	constexpr float RestartedFlowMaxSeconds = 1e-4f;
	constexpr int32 FlowRestartStepCount = 6;
	constexpr float LiveActiveSeconds = 600.0f;
	constexpr int32 MaxLoadStepCount = 400;
	constexpr int32 SettleStepCount = 8;
	constexpr int32 CaptureWidth = 640;
	constexpr int32 CaptureHeight = 360;
	constexpr int32 CaptureWarmupCount = 5;
	constexpr int32 CaptureMeasuredCount = 30;
	constexpr double SlabToleranceKm = 1e-3;
	constexpr double SuspectDifferenceMilliseconds = 0.02;
	constexpr float ScriptedBottomKm = 5.0f;

	struct FTDCloudTestWorld
	{
		FTDCloudTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidCloudLayersWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDCloudTestWorld()
		{
			if (!World)
			{
				return;
			}

			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			FlushRenderingCommands();
		}

		void StepWorld()
		{
			++GFrameCounter;
			FlushAsyncLoading();
			World->Tick(LEVELTICK_All, StepSeconds);
			FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, StepSeconds);
			FlushRenderingCommands();
		}

		UWorld* World = nullptr;
	};

	struct FTDConsoleRestore
	{
		explicit FTDConsoleRestore(const TCHAR* InName)
			: Variable(IConsoleManager::Get().FindConsoleVariable(InName))
		{
			if (Variable)
			{
				OriginalValue = Variable->GetString();
			}
		}

		~FTDConsoleRestore()
		{
			if (Variable)
			{
				Variable->Set(*OriginalValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		FString OriginalValue;
	};

	FTDFluidCloudSlotBand MakeBand(float BottomKm, float ThicknessKm, float TerrainFollow = 0.0f, float LobeDepthRatio = 0.0f, bool bIsActive = true)
	{
		FTDFluidCloudSlotBand Band;
		Band.bIsActive = bIsActive;
		Band.BottomKm = BottomKm;
		Band.ThicknessKm = ThicknessKm;
		Band.TerrainFollow = TerrainFollow;
		Band.LobeDepthRatio = LobeDepthRatio;
		return Band;
	}

	bool TestSlab(FAutomationTestBase& Test, const TCHAR* Label, const TArray<FTDFluidCloudSlotBand>& Bands, const FVector2D& TerrainRangeKm, const FVector2D& ExpectedKm)
	{
		const FVector2D SlabKm = ATDFluidCloudLayersActor::ComputeSlabKm(Bands, TerrainRangeKm);
		const bool bMatches = FMath::Abs(SlabKm.X - ExpectedKm.X) <= SlabToleranceKm && FMath::Abs(SlabKm.Y - ExpectedKm.Y) <= SlabToleranceKm;
		return Test.TestTrue(*FString::Printf(TEXT("%s: slab (%.4f, %.4f) equals (%.4f, %.4f)"), Label, SlabKm.X, SlabKm.Y, ExpectedKm.X, ExpectedKm.Y), bMatches);
	}

	bool CanRunCloudRhiTest(FAutomationTestBase& Test, const TCHAR* Label)
	{
		if (!GUsingNullRHI)
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI."), Label));
		return false;
	}

	ATDFluidLiveActor* SpawnSourceLive(FAutomationTestBase& Test, UWorld& World, const FName& SourceTag, const FVector& Location)
	{
		UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(&World);
		if (!Test.TestNotNull(TEXT("UTDFluidLiveSubsystem exists"), Subsystem))
		{
			return nullptr;
		}

		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(&World, ETDFluidPresetKind::Smoke);
		ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(Preset, Location, LiveActiveSeconds);
		if (!Test.TestNotNull(*FString::Printf(TEXT("spawned the source live %s"), *SourceTag.ToString()), LiveActor))
		{
			return nullptr;
		}

		LiveActor->Tags.AddUnique(SourceTag);
		return LiveActor;
	}

	FTDFluidCloudSetup MakeSetup(const TCHAR* SetupName, const TCHAR* MaterialPath, const TArray<TPair<int32, FName>>& Sources)
	{
		FTDFluidCloudSetup Setup;
		Setup.SetupName = FName(SetupName);
		Setup.CloudMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(MaterialPath));
		for (const TPair<int32, FName>& Source : Sources)
		{
			FTDFluidCloudSlotSource& SlotSource = Setup.SlotSources.AddDefaulted_GetRef();
			SlotSource.SlotIndex = Source.Key;
			SlotSource.Role = ETDFluidCloudSlotTextureRole::Coverage;
			SlotSource.LiveActorTag = Source.Value;
		}
		return Setup;
	}

	ATDFluidCloudLayersActor* SpawnCloudActor(UWorld& World, const TArray<FTDFluidCloudSetup>& Setups)
	{
		ATDFluidCloudLayersActor* CloudActor = World.SpawnActorDeferred<ATDFluidCloudLayersActor>(ATDFluidCloudLayersActor::StaticClass(), FTransform::Identity);
		if (!CloudActor)
		{
			return nullptr;
		}

		CloudActor->Setups = Setups;
		CloudActor->DefaultSetupIndex = 0;
		CloudActor->FinishSpawning(FTransform::Identity);
		return CloudActor;
	}

	bool WaitForCloudMaterial(FAutomationTestBase& Test, FTDCloudTestWorld& TestWorld, const ATDFluidCloudLayersActor& CloudActor, const TCHAR* MaterialPath)
	{
		for (int32 StepIndex = 0; StepIndex < MaxLoadStepCount && !CloudActor.GetActiveMaterial(); ++StepIndex)
		{
			TestWorld.StepWorld();
		}

		if (CloudActor.GetActiveMaterial())
		{
			return true;
		}

		Test.AddError(FString::Printf(TEXT("the cloud material never activated; %s is missing (run editor_make_fluid_cloud_material.py)"), MaterialPath));
		return false;
	}

	float ReadFlowSeconds(UMaterialInstanceDynamic& Material)
	{
		return Material.K2_GetScalarParameterValue(CloudFlowParameterName);
	}

	bool TestFlowSinceRestart(FAutomationTestBase& Test, const TCHAR* Label, UMaterialInstanceDynamic& Material, const UWorld& World, double RestartWorldSeconds)
	{
		const float FlowSeconds = ReadFlowSeconds(Material);
		const double ExpectedSeconds = World.GetTimeSeconds() - RestartWorldSeconds;
		return Test.TestTrue(*FString::Printf(TEXT("%s: flow seconds %.5f equal world seconds since the restart %.5f"), Label, FlowSeconds, ExpectedSeconds), FMath::Abs(FlowSeconds - ExpectedSeconds) <= FlowToleranceSeconds);
	}

	void WriteReportJson(const FString& Path, const TSharedRef<FJsonObject>& Report)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Report, Writer);
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	double MedianOf(TArray<double> Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}

		Values.Sort();
		return Values[Values.Num() / 2];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudLayersSlabCoversActiveBandsTest,
	"TDGame.FluidFX.CloudLayers.SlabCoversActiveBands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudLayersSlabCoversActiveBandsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudLayersTestPrivate;

	const FVector2D NoTerrain = FVector2D::ZeroVector;
	TestSlab(*this, TEXT("single band"), { MakeBand(1.5f, 3.0f) }, NoTerrain, FVector2D(1.5, 3.0));
	TestSlab(*this, TEXT("two bands"), { MakeBand(0.2f, 0.3f), MakeBand(2.0f, 4.0f) }, NoTerrain, FVector2D(0.2, 5.8));
	TestSlab(*this, TEXT("inactive band ignored"), { MakeBand(0.1f, 0.1f, 0.0f, 0.0f, false), MakeBand(2.0f, 1.0f) }, NoTerrain, FVector2D(2.0, 1.0));
	TestSlab(*this, TEXT("terrain follow"), { MakeBand(0.02f, 0.3f, 1.0f) }, FVector2D(0.1, 2.5), FVector2D(0.12, 2.70));
	TestSlab(*this, TEXT("mammatus lobes"), { MakeBand(1.0f, 2.0f, 0.0f, 0.3f) }, NoTerrain, FVector2D(0.4, 2.6));
	TestSlab(*this, TEXT("no active band"), {}, NoTerrain, FVector2D(0.0, 0.0));
	TestSlab(*this, TEXT("minimum layer height"), { MakeBand(1.0f, 0.01f) }, NoTerrain, FVector2D(1.0, 0.1));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudLayersNearSamplingHysteresisTest,
	"TDGame.FluidFX.CloudLayers.NearSamplingHysteresis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudLayersNearSamplingHysteresisTest::RunTest(const FString& Parameters)
{
	const FVector2D SlabKm(1.5, 3.0);
	const float Altitudes[] = { 0.0f, 1.0f, 0.8f, 0.7f, 5.0f, 5.2f, 5.3f };
	const bool Expected[] = { false, true, true, false, true, true, false };
	bool bIsNear = false;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Altitudes); ++Index)
	{
		bIsNear = ATDFluidCloudLayersActor::ResolveNearSampling(Altitudes[Index], SlabKm, 0.5f, 0.75f, bIsNear);
		TestEqual(*FString::Printf(TEXT("altitude %.2f km near"), Altitudes[Index]), bIsNear, Expected[Index]);
	}
	TestFalse(TEXT("an empty slab never samples near"), ATDFluidCloudLayersActor::ResolveNearSampling(1.0f, FVector2D::ZeroVector, 0.5f, 0.75f, true));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudLayersSetupIndexTest,
	"TDGame.FluidFX.CloudLayers.SetupIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudLayersSetupIndexTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("console -1 keeps the default"), ATDFluidCloudLayersActor::ResolveSetupIndex(-1, 3, 17), 3);
	TestEqual(TEXT("console 5 selects setup 5"), ATDFluidCloudLayersActor::ResolveSetupIndex(5, 3, 17), 5);
	TestEqual(TEXT("console out of range keeps the default"), ATDFluidCloudLayersActor::ResolveSetupIndex(20, 3, 17), 3);
	TestEqual(TEXT("default out of range falls back to 0"), ATDFluidCloudLayersActor::ResolveSetupIndex(-1, 20, 17), 0);
	TestEqual(TEXT("no setups gives INDEX_NONE"), ATDFluidCloudLayersActor::ResolveSetupIndex(-1, 0, 0), static_cast<int32>(INDEX_NONE));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudLayersSlotParameterNamesTest,
	"TDGame.FluidFX.CloudLayers.SlotParameterNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudLayersSlotParameterNamesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("slot 2 coverage texture name"), ATDFluidCloudLayersActor::MakeSlotParameterName(2, TEXT("CoverageTexture")) == FName(TEXT("CloudSlot2CoverageTexture")));
	TestTrue(TEXT("slot 0 section texture name"), ATDFluidCloudLayersActor::MakeSlotParameterName(0, TEXT("SectionTexture")) == FName(TEXT("CloudSlot0SectionTexture")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudLayersFlowClockRestartsTest,
	"TDGame.FluidFX.CloudLayers.FlowClockRestarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudLayersFlowClockRestartsTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudLayersTestPrivate;

	TestNearlyEqual(TEXT("flow seconds count from the origin"), ATDFluidCloudLayersActor::ComputeCloudFlowSeconds(10.0, 4.0), 6.0f, 1e-6f);
	TestNearlyEqual(TEXT("flow seconds before the origin stay at zero"), ATDFluidCloudLayersActor::ComputeCloudFlowSeconds(4.0, 10.0), 0.0f, 1e-6f);
	TestNearlyEqual(TEXT("flow seconds keep the fraction late in a session"), ATDFluidCloudLayersActor::ComputeCloudFlowSeconds(100000.25, 100000.0), 0.25f, 1e-6f);

	FTDConsoleRestore SetupRestore(SetupConsoleName);
	if (!TestNotNull(TEXT("TD.FluidFX.CloudSetup exists"), SetupRestore.Variable))
	{
		return false;
	}
	SetupRestore.Variable->Set(-1, ECVF_SetByCode);

	FTDCloudTestWorld TestWorld;
	ATDFluidCloudLayersActor* CloudActor = SpawnCloudActor(*TestWorld.World, { MakeSetup(TEXT("SkyOnlyA"), SkyOnlyMaterialPath, {}), MakeSetup(TEXT("SkyOnlyB"), SkyOnlyMaterialPath, {}) });
	if (!TestNotNull(TEXT("spawned the cloud layers actor"), CloudActor) || !WaitForCloudMaterial(*this, TestWorld, *CloudActor, SkyOnlyMaterialPath))
	{
		return false;
	}

	float ParentFlowSeconds = -1.0f;
	const UMaterialInterface* SkyOnlyMaterial = CloudActor->Setups[0].CloudMaterial.Get();
	const bool bParentHasFlowParameter = SkyOnlyMaterial && SkyOnlyMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(CloudFlowParameterName), ParentFlowSeconds);
	if (!TestTrue(TEXT("the SkyOnly parent has CloudFlowSeconds (run editor_make_fluid_cloud_material.py)"), bParentHasFlowParameter))
	{
		return false;
	}
	TestEqual(TEXT("the SkyOnly parent keeps CloudFlowSeconds at 0"), ParentFlowSeconds, 0.0f);

	for (int32 StepIndex = 0; StepIndex < SettleStepCount; ++StepIndex)
	{
		TestWorld.StepWorld();
	}
	UMaterialInstanceDynamic* FirstMaterial = CloudActor->GetActiveMaterial();
	if (!TestNotNull(TEXT("setup 0 has a dynamic material"), FirstMaterial))
	{
		return false;
	}
	const float SettledFlowSeconds = ReadFlowSeconds(*FirstMaterial);
	const double SettledWorldSeconds = TestWorld.World->GetTimeSeconds();
	TestTrue(*FString::Printf(TEXT("without a restart flow seconds %.5f follow world seconds %.5f"), SettledFlowSeconds, SettledWorldSeconds), FMath::Abs(SettledFlowSeconds - SettledWorldSeconds) <= StepSeconds + FlowToleranceSeconds);
	TestTrue(*FString::Printf(TEXT("flow seconds %.5f run before any restart"), SettledFlowSeconds), SettledFlowSeconds > 0.0f);

	for (int32 StepIndex = 0; StepIndex < SettleStepCount; ++StepIndex)
	{
		TestWorld.StepWorld();
	}
	const float AdvancedFlowSeconds = ReadFlowSeconds(*FirstMaterial);
	const double ExpectedAdvanceSeconds = SettleStepCount * StepSeconds;
	TestTrue(*FString::Printf(TEXT("%d more steps add %.5f flow seconds (expected %.5f)"), SettleStepCount, AdvancedFlowSeconds - SettledFlowSeconds, ExpectedAdvanceSeconds), FMath::Abs((AdvancedFlowSeconds - SettledFlowSeconds) - ExpectedAdvanceSeconds) <= FlowToleranceSeconds);

	CloudActor->RestartCloudFlowClock();
	const double RestartWorldSeconds = TestWorld.World->GetTimeSeconds();
	const float RestartedFlowSeconds = ReadFlowSeconds(*FirstMaterial);
	TestTrue(*FString::Printf(TEXT("the restart puts flow seconds at %.6f"), RestartedFlowSeconds), RestartedFlowSeconds >= 0.0f && RestartedFlowSeconds <= RestartedFlowMaxSeconds);
	for (int32 StepIndex = 0; StepIndex < FlowRestartStepCount; ++StepIndex)
	{
		TestWorld.StepWorld();
	}
	TestFlowSinceRestart(*this, TEXT("after the restart"), *FirstMaterial, *TestWorld.World, RestartWorldSeconds);

	SetupRestore.Variable->Set(1, ECVF_SetByCode);
	TestWorld.StepWorld();
	UMaterialInstanceDynamic* SecondMaterial = CloudActor->GetActiveMaterial();
	TestEqual(TEXT("console switch activates setup 1"), CloudActor->GetActiveSetupIndex(), 1);
	if (!TestTrue(TEXT("setup 1 owns its own dynamic material"), SecondMaterial && SecondMaterial != FirstMaterial))
	{
		return false;
	}
	TestFlowSinceRestart(*this, TEXT("the new setup material"), *SecondMaterial, *TestWorld.World, RestartWorldSeconds);

	UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>();
	if (!TestNotNull(TEXT("preset tuning subsystem exists"), TuningSubsystem))
	{
		return false;
	}
	TestEqual(TEXT("tuning reload has no errors"), TuningSubsystem->ReloadPresetTuning(), 0);
	TestWorld.StepWorld();
	TestFlowSinceRestart(*this, TEXT("after a tuning reload"), *SecondMaterial, *TestWorld.World, RestartWorldSeconds);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudLayersRhiBindsSlotTexturesTest,
	"TDGame.FluidFXRhi.CloudLayers.BindsSlotTextures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudLayersRhiBindsSlotTexturesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudLayersTestPrivate;

	const TCHAR* const Label = TEXT("TDGame.FluidFXRhi.CloudLayers.BindsSlotTextures");
	if (!CanRunCloudRhiTest(*this, Label))
	{
		return false;
	}

	FTDConsoleRestore SetupRestore(SetupConsoleName);
	if (!TestNotNull(TEXT("TD.FluidFX.CloudSetup exists"), SetupRestore.Variable))
	{
		return false;
	}
	SetupRestore.Variable->Set(-1, ECVF_SetByCode);

	FTDCloudTestWorld TestWorld;
	ATDFluidLiveActor* LiveA = SpawnSourceLive(*this, *TestWorld.World, SourceTagA, FVector(0.0, 0.0, -20000.0));
	ATDFluidLiveActor* LiveB = SpawnSourceLive(*this, *TestWorld.World, SourceTagB, FVector(5000.0, 0.0, -20000.0));
	if (!LiveA || !LiveB)
	{
		return false;
	}

	const TArray<FTDFluidCloudSetup> Setups = {
		MakeSetup(TEXT("TestSlots"), TestSlotsMaterialPath, { TPair<int32, FName>(1, SourceTagA), TPair<int32, FName>(2, SourceTagB) }),
		MakeSetup(TEXT("TestSlotsB"), TestSlotsMaterialPath, { TPair<int32, FName>(1, SourceTagB) }),
	};
	ATDFluidCloudLayersActor* CloudActor = SpawnCloudActor(*TestWorld.World, Setups);
	if (!TestNotNull(TEXT("spawned the cloud layers actor"), CloudActor) || !WaitForCloudMaterial(*this, TestWorld, *CloudActor, TestSlotsMaterialPath))
	{
		return false;
	}
	for (int32 StepIndex = 0; StepIndex < SettleStepCount; ++StepIndex)
	{
		TestWorld.StepWorld();
	}

	UMaterialInstanceDynamic* FirstMaterial = CloudActor->GetActiveMaterial();
	TestEqual(TEXT("setup 0 is active"), CloudActor->GetActiveSetupIndex(), 0);
	TestTrue(TEXT("slot 1 coverage is the first live density texture"), FirstMaterial->K2_GetTextureParameterValue(FName(TEXT("CloudSlot1CoverageTexture"))) == LiveA->LiveComponent->GetDensityPressureTexture());
	TestTrue(TEXT("slot 2 coverage is the second live density texture"), FirstMaterial->K2_GetTextureParameterValue(FName(TEXT("CloudSlot2CoverageTexture"))) == LiveB->LiveComponent->GetDensityPressureTexture());
	const FVector2D SlabKm = CloudActor->GetActiveSlabKm();
	TestTrue(*FString::Printf(TEXT("slab (%.4f, %.4f) equals (1, 3)"), SlabKm.X, SlabKm.Y), FMath::Abs(SlabKm.X - 1.0) <= SlabToleranceKm && FMath::Abs(SlabKm.Y - 3.0) <= SlabToleranceKm);
	TestTrue(TEXT("both sources are awake in setup 0"), LiveA->LiveComponent->IsSimulationAwake() && LiveB->LiveComponent->IsSimulationAwake());
	TestTrue(TEXT("cloud component is visible"), CloudActor->CloudComponent->IsVisible());

	SetupRestore.Variable->Set(1, ECVF_SetByCode);
	for (int32 StepIndex = 0; StepIndex < SettleStepCount; ++StepIndex)
	{
		TestWorld.StepWorld();
	}
	TestEqual(TEXT("console switch activates setup 1"), CloudActor->GetActiveSetupIndex(), 1);
	TestTrue(TEXT("setup 1 owns its own dynamic material"), CloudActor->GetActiveMaterial() && CloudActor->GetActiveMaterial() != FirstMaterial);
	TestFalse(TEXT("the first source sleeps in setup 1"), LiveA->LiveComponent->IsSimulationAwake());
	TestTrue(TEXT("the second source stays awake in setup 1"), LiveB->LiveComponent->IsSimulationAwake());

	UMaterialInstanceDynamic* SecondMaterial = CloudActor->GetActiveMaterial();
	const float LookBottomKm = SecondMaterial->K2_GetScalarParameterValue(FName(TEXT("CloudSlot1BottomKm")));
	SecondMaterial->SetScalarParameterValue(FName(TEXT("CloudSlot1BottomKm")), ScriptedBottomKm);
	UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>();
	if (TestNotNull(TEXT("preset tuning subsystem exists"), TuningSubsystem))
	{
		TestEqual(TEXT("tuning reload has no errors"), TuningSubsystem->ReloadPresetTuning(), 0);
		TestWorld.StepWorld();
		TestEqual(TEXT("reload restores the look scalar"), SecondMaterial->K2_GetScalarParameterValue(FName(TEXT("CloudSlot1BottomKm"))), LookBottomKm);
		TestTrue(TEXT("reload keeps the slab bottom at the look value"), FMath::Abs(CloudActor->GetActiveSlabKm().X - LookBottomKm) <= SlabToleranceKm);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidCloudLayersRhiOffscreenCostTest,
	"TDGame.FluidFXRhi.CloudLayers.OffscreenCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidCloudLayersRhiOffscreenCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCloudLayersTestPrivate;

	const TCHAR* const Label = TEXT("TDGame.FluidFXRhi.CloudLayers.OffscreenCost");
	if (!CanRunCloudRhiTest(*this, Label))
	{
		return false;
	}

	FTDConsoleRestore SetupRestore(SetupConsoleName);
	FTDConsoleRestore ScaleRestore(ViewScaleConsoleName);
	if (!TestTrue(TEXT("cloud console variables exist"), SetupRestore.Variable && ScaleRestore.Variable))
	{
		return false;
	}
	SetupRestore.Variable->Set(-1, ECVF_SetByCode);

	FTDCloudTestWorld TestWorld;
	AActor* RigActor = TestWorld.World->SpawnActor<AActor>();
	USkyAtmosphereComponent* Atmosphere = NewObject<USkyAtmosphereComponent>(RigActor);
	Atmosphere->RegisterComponent();
	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(RigActor);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetWorldRotation(FRotator(-25.0, 30.0, 0.0));
	Sun->RegisterComponent();
	UTextureRenderTarget2D* CaptureTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	CaptureTarget->InitAutoFormat(CaptureWidth, CaptureHeight);
	CaptureTarget->UpdateResourceImmediate(true);
	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(RigActor);
	Capture->TextureTarget = CaptureTarget;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->SetWorldLocationAndRotation(FVector(0.0, 0.0, 200.0), FRotator(15.0, 0.0, 0.0));
	Capture->RegisterComponent();

	ATDFluidCloudLayersActor* CloudActor = SpawnCloudActor(*TestWorld.World, { MakeSetup(TEXT("SkyOnly"), SkyOnlyMaterialPath, {}) });
	if (!TestNotNull(TEXT("spawned the cloud layers actor"), CloudActor) || !WaitForCloudMaterial(*this, TestWorld, *CloudActor, SkyOnlyMaterialPath))
	{
		return false;
	}

	const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Cases;
	TMap<FString, double> MedianByCase;
	for (const float ViewScale : { 2.0f, 4.0f })
	{
		for (const bool bIsVisible : { true, false })
		{
			ScaleRestore.Variable->Set(ViewScale, ECVF_SetByCode);
			TestWorld.StepWorld();
			CloudActor->CloudComponent->SetVisibility(bIsVisible);
			TArray<double> Milliseconds;
			for (int32 CaptureIndex = 0; CaptureIndex < CaptureWarmupCount + CaptureMeasuredCount; ++CaptureIndex)
			{
				TestWorld.StepWorld();
				const double StartSeconds = FPlatformTime::Seconds();
				Capture->CaptureScene();
				ENQUEUE_RENDER_COMMAND(TDFluidCloudCaptureWait)([](FRHICommandListImmediate& RHICmdList)
				{
					RHICmdList.SubmitAndBlockUntilGPUIdle();
				});
				FlushRenderingCommands();
				if (CaptureIndex >= CaptureWarmupCount)
				{
					Milliseconds.Add((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
				}
			}

			const FString CaseName = FString::Printf(TEXT("scale%.0f_%s"), ViewScale, bIsVisible ? TEXT("visible") : TEXT("hidden"));
			const double MedianMilliseconds = MedianOf(Milliseconds);
			MedianByCase.Add(CaseName, MedianMilliseconds);
			const TSharedRef<FJsonObject> CaseObject = MakeShared<FJsonObject>();
			CaseObject->SetStringField(TEXT("case"), CaseName);
			CaseObject->SetNumberField(TEXT("view_sample_scale"), ViewScale);
			CaseObject->SetBoolField(TEXT("cloud_visible"), bIsVisible);
			CaseObject->SetNumberField(TEXT("samples"), Milliseconds.Num());
			CaseObject->SetNumberField(TEXT("median_capture_ms"), MedianMilliseconds);
			Cases.Add(MakeShared<FJsonValueObject>(CaseObject));
			AddInfo(FString::Printf(TEXT("TDFluidCloudCost: %s median_ms=%.4f samples=%d"), *CaseName, MedianMilliseconds, Milliseconds.Num()));
			TestTrue(*FString::Printf(TEXT("%s collected captures"), *CaseName), Milliseconds.Num() == CaptureMeasuredCount);
		}
	}

	const double VisibleCostMilliseconds = MedianByCase.FindRef(TEXT("scale2_visible")) - MedianByCase.FindRef(TEXT("scale2_hidden"));
	Report->SetArrayField(TEXT("cases"), Cases);
	Report->SetNumberField(TEXT("visible_minus_hidden_scale2_ms"), VisibleCostMilliseconds);
	Report->SetBoolField(TEXT("capture_excludes_cloud_suspect"), FMath::Abs(VisibleCostMilliseconds) < SuspectDifferenceMilliseconds);
	Report->SetStringField(TEXT("note"), TEXT("Wall-clock capture plus GPU idle per capture, recorded only; PIE CSV GPU/VolumetricCloud decides the budget."));
	const FString ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentOps"), FDateTime::Now().ToString(TEXT("%Y%m%d")), TEXT("fx11r"), TEXT("cloud_offscreen_cost.json"));
	WriteReportJson(ReportPath, Report);
	AddInfo(FString::Printf(TEXT("TDFluidCloudCost report: %s"), *ReportPath));
	return !HasAnyErrors();
}

#endif
