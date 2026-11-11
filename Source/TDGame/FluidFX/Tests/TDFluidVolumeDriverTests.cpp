#if WITH_DEV_AUTOMATION_TESTS

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidRenderProfile.h"
#include "FluidFX/TDFluidVolumeDriverComponent.h"
#include "FluidFX/TDFluidVolumeDriverTypes.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "LandscapeComponent.h"
#include "LandscapeNaniteComponent.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace TDFluidVolumeDriverTestPrivate
{
	const FName TargetTag(TEXT("TDFluidVolumeDriverTestTarget"));

	struct FDriverTestWorld
	{
		UWorld* World = nullptr;

		FDriverTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidVolumeDriverWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FDriverTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	};

	UStaticMeshComponent* SpawnTaggedMeshActor(UWorld& World, UMaterialInterface* OriginalMaterial)
	{
		AActor* Actor = World.SpawnActor<AActor>();
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Actor);
		Actor->SetRootComponent(Mesh);
		Mesh->RegisterComponent();
		Mesh->SetMaterial(0, OriginalMaterial);
		Actor->Tags.Add(TargetTag);
		return Mesh;
	}

	TArray<ETDFluidPresetKind> GetKindsWithoutDriver()
	{
		TArray<ETDFluidPresetKind> Kinds;
		for (uint8 KindValue = 0; KindValue <= static_cast<uint8>(ETDFluidPresetKind::SmokePiston); ++KindValue)
		{
			Kinds.Add(static_cast<ETDFluidPresetKind>(KindValue));
		}
		return Kinds;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeDriverPlacementMathTest,
	"TDGame.FluidFX.VolumeDriver.PlacementMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeDriverPlacementMathTest::RunTest(const FString& Parameters)
{
	const TArray<FTDFluidPlaneBasis> Bases = {
		FTDFluidPlaneBasis::MakeWorldFacing(),
		FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, FRotator(-35.0, 40.0, 0.0), 0.0f),
		FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::VerticalBillboard, FRotator(-20.0, -70.0, 0.0), 10.0f)
	};
	const FVector PlaneCenter(120.0, -340.0, 55.0);
	const FVector2D ExtentCm(700.0, 500.0);
	for (const FTDFluidPlaneBasis& Basis : Bases)
	{
		FTDFluidVolumeDriverSettings Settings;
		Settings.HeterogeneousVolumeHeightCm = 300.0f;
		Settings.HeterogeneousVolumeSizeMultiplier = 1.5f;
		Settings.HeterogeneousVolumeOffsetCm = FVector(10.0, -20.0, 30.0);
		const FVector ExpectedSize(ExtentCm.X * 1.5, ExtentCm.Y * 1.5, 300.0);
		for (const int32 Resolution : { 64, 256 })
		{
			Settings.HeterogeneousVolumeResolutionXY = Resolution;
			Settings.HeterogeneousVolumeResolutionZ = Resolution / 2;
			const FTDFluidVolumePlacement Placement = UTDFluidVolumeDriverComponent::ComputeHeterogeneousVolumePlacement(Basis, PlaneCenter, ExtentCm, Settings);
			const FVector Size = Placement.Transform.GetScale3D() * FVector(Placement.Resolution);
			TestTrue(*FString::Printf(TEXT("Scale x resolution equals size at %d (%s)"), Resolution, *Size.ToString()), Size.Equals(ExpectedSize, 1.0e-3));
			TestTrue(TEXT("Rotation follows the plane basis"), Placement.Transform.GetRotation().Equals(Basis.GetRotation(), 1.0e-6));
			FVector ExpectedCenter = PlaneCenter + Basis.U * 10.0 + Basis.V * -20.0 + Basis.N * 30.0;
			if (Basis.IsWorldFacing())
			{
				ExpectedCenter += Basis.N * 150.0;
			}
			TestTrue(TEXT("Center adds the offset along the basis"), Placement.Transform.GetLocation().Equals(ExpectedCenter, 1.0e-3));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeDriverFogFloorFollowGateTest,
	"TDGame.FluidFX.VolumeDriver.FogFloorFollowGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeDriverFogFloorFollowGateTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Default settings keep the fog floor flat"), UTDFluidVolumeDriverComponent::IsFogFloorFollowActive(FTDFluidVolumeDriverSettings(), FTDFluidTerrainHeightSettings(), true, true));
	constexpr int32 GateConditionCount = 4;
	for (int32 Combination = 0; Combination < (1 << GateConditionCount); ++Combination)
	{
		const bool bFollowsTerrain = (Combination & 1) != 0;
		const bool bUsesSceneCapture = (Combination & 2) != 0;
		const bool bHasHeightTexture = (Combination & 4) != 0;
		const bool bIsWorldFacing = (Combination & 8) != 0;
		FTDFluidVolumeDriverSettings Driver;
		Driver.bFogFloorFollowsTerrain = bFollowsTerrain;
		FTDFluidTerrainHeightSettings Terrain;
		Terrain.VolumeHeightSource = bUsesSceneCapture ? ETDFluidVolumeHeightSource::SceneCapture : ETDFluidVolumeHeightSource::HeightCache;
		const bool bExpected = bFollowsTerrain && bUsesSceneCapture && bHasHeightTexture && bIsWorldFacing;
		const bool bIsActive = UTDFluidVolumeDriverComponent::IsFogFloorFollowActive(Driver, Terrain, bHasHeightTexture, bIsWorldFacing);
		TestEqual(*FString::Printf(TEXT("Gate follow=%d capture=%d texture=%d world=%d"), bFollowsTerrain ? 1 : 0, bUsesSceneCapture ? 1 : 0, bHasHeightTexture ? 1 : 0, bIsWorldFacing ? 1 : 0), bIsActive, bExpected);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeDriverLandscapeSurfaceComponentTest,
	"TDGame.FluidFX.VolumeDriver.LandscapeSurfaceComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeDriverLandscapeSurfaceComponentTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Landscape render component is terrain"), UTDFluidVolumeDriverComponent::IsLandscapeSurfaceComponent(*GetDefault<ULandscapeComponent>()));
	TestTrue(TEXT("Landscape Nanite component is terrain"), UTDFluidVolumeDriverComponent::IsLandscapeSurfaceComponent(*GetDefault<ULandscapeNaniteComponent>()));
	TestFalse(TEXT("Grass base class (hierarchical instances) is not terrain"), UTDFluidVolumeDriverComponent::IsLandscapeSurfaceComponent(*GetDefault<UHierarchicalInstancedStaticMeshComponent>()));
	TestFalse(TEXT("Instanced static mesh is not terrain"), UTDFluidVolumeDriverComponent::IsLandscapeSurfaceComponent(*GetDefault<UInstancedStaticMeshComponent>()));
	TestFalse(TEXT("Static mesh is not terrain"), UTDFluidVolumeDriverComponent::IsLandscapeSurfaceComponent(*GetDefault<UStaticMeshComponent>()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeDriverHeightCaptureRendererTest,
	"TDGame.FluidFX.VolumeDriver.HeightCaptureRenderer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeDriverHeightCaptureRendererTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Terrain-only capture renders inside the main renderer when a game viewport draws the world"), UTDFluidVolumeDriverComponent::ShouldRenderHeightCaptureInMainRenderer(ETDFluidHeightCaptureContent::TerrainOnly, true));
	TestFalse(TEXT("Terrain-only capture keeps its own renderer without a game viewport"), UTDFluidVolumeDriverComponent::ShouldRenderHeightCaptureInMainRenderer(ETDFluidHeightCaptureContent::TerrainOnly, false));
	TestFalse(TEXT("All-geometry capture keeps its own renderer with a game viewport"), UTDFluidVolumeDriverComponent::ShouldRenderHeightCaptureInMainRenderer(ETDFluidHeightCaptureContent::AllGeometry, true));
	TestFalse(TEXT("All-geometry capture keeps its own renderer without a game viewport"), UTDFluidVolumeDriverComponent::ShouldRenderHeightCaptureInMainRenderer(ETDFluidHeightCaptureContent::AllGeometry, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeDriverHeightCaptureCoverMultiplierTest,
	"TDGame.FluidFX.VolumeDriver.HeightCaptureCoverMultiplier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeDriverHeightCaptureCoverMultiplierTest::RunTest(const FString& Parameters)
{
	struct FCoverCase
	{
		bool bFollows;
		bool bWraps;
		float SizeMultiplier;
		float Expected;
	};
	const FCoverCase Cases[] = {
		{ false, false, 3.0f, 1.0f },
		{ true, false, 3.0f, 1.0f },
		{ false, true, 3.0f, 1.0f },
		{ true, true, 3.0f, 3.0f * UE_SQRT_2 },
		{ true, true, 0.5f, UE_SQRT_2 },
		{ true, true, 1.0f, UE_SQRT_2 },
	};
	for (const FCoverCase& Case : Cases)
	{
		const float Actual = UTDFluidVolumeDriverComponent::GetHeightCaptureCoverMultiplier(Case.bFollows, Case.bWraps, Case.SizeMultiplier);
		TestTrue(*FString::Printf(TEXT("Cover multiplier for follow %d wrap %d box %.2f is %.4f (got %.4f)"), Case.bFollows ? 1 : 0, Case.bWraps ? 1 : 0, Case.SizeMultiplier, Case.Expected, Actual), FMath::IsNearlyEqual(Actual, Case.Expected, 1.0e-4f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeDriverSlotResolveAndNeedTest,
	"TDGame.FluidFX.VolumeDriver.SlotResolveAndNeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeDriverSlotResolveAndNeedTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumeDriverTestPrivate;
	FTDFluidOutputTargetSettings Targets;
	Targets.PrimaryOutputMaterialIndex = 5;
	Targets.SecondaryOutputMaterialIndex = 1;
	TestEqual(TEXT("Index clamps to the array"), UTDFluidVolumeDriverComponent::ResolveSlotMaterialIndex(ETDFluidOutputMaterialSlot::Primary, Targets, 3), 2);
	TestEqual(TEXT("Index stays when valid"), UTDFluidVolumeDriverComponent::ResolveSlotMaterialIndex(ETDFluidOutputMaterialSlot::Secondary, Targets, 3), 1);
	TestEqual(TEXT("Empty array gives index 0"), UTDFluidVolumeDriverComponent::ResolveSlotMaterialIndex(ETDFluidOutputMaterialSlot::Tertiary, Targets, 0), 0);

	UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), ETDFluidPresetKind::Smoke);
	const TSoftObjectPtr<UMaterialInterface> OutputPath(FSoftObjectPath(TEXT("/Game/FluidFX/Test/M_Output.M_Output")));
	const TSoftObjectPtr<UMaterialInterface> VolumePath(FSoftObjectPath(TEXT("/Game/FluidFX/Test/M_Volume.M_Volume")));
	const TSoftObjectPtr<UMaterialInterface> ArrayPath(FSoftObjectPath(TEXT("/Game/FluidFX/Test/M_Array1.M_Array1")));
	Preset->OutputMaterial = OutputPath;
	Preset->VolumeMaterial = VolumePath;
	TestTrue(TEXT("Primary falls back to OutputMaterial"), Preset->ResolveSlotMaterial(ETDFluidOutputMaterialSlot::Primary, 0) == OutputPath);
	TestTrue(TEXT("Secondary falls back to VolumeMaterial"), Preset->ResolveSlotMaterial(ETDFluidOutputMaterialSlot::Secondary, 0) == VolumePath);
	TestTrue(TEXT("Tertiary has no fallback"), Preset->ResolveSlotMaterial(ETDFluidOutputMaterialSlot::Tertiary, 0).IsNull());
	Preset->SecondaryOutputMaterials = { VolumePath, ArrayPath };
	TestTrue(TEXT("Array entry wins over the fallback"), Preset->ResolveSlotMaterial(ETDFluidOutputMaterialSlot::Secondary, 1) == ArrayPath);
	TestTrue(TEXT("Array index clamps"), Preset->ResolveSlotMaterial(ETDFluidOutputMaterialSlot::Secondary, 9) == ArrayPath);

	TestFalse(TEXT("Surface material is not a volume material"), UTDFluidVolumeDriverComponent::IsVolumeDomainMaterial(UMaterial::GetDefaultMaterial(MD_Surface)));
	TestFalse(TEXT("Null material is not a volume material"), UTDFluidVolumeDriverComponent::IsVolumeDomainMaterial(nullptr));

	for (const ETDFluidPresetKind Kind : GetKindsWithoutDriver())
	{
		const UTDFluidPresetAsset* KindPreset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), Kind);
		const bool bIsNeeded = UTDFluidVolumeDriverComponent::IsDriverNeeded(UTDFluidPresetAsset::BuildVolumeDriverForPresetKind(Kind), UTDFluidPresetAsset::BuildTerrainHeightForPresetKind(Kind), KindPreset);
		TestFalse(*FString::Printf(TEXT("Kind %d needs no driver"), static_cast<int32>(Kind)), bIsNeeded);
	}
	for (const ETDFluidPresetKind Kind : { ETDFluidPresetKind::SmokeHeterogeneous, ETDFluidPresetKind::VortexHeterogeneous, ETDFluidPresetKind::SmokeCameraFacing, ETDFluidPresetKind::FloorSmokeCapture })
	{
		const bool bIsNeeded = UTDFluidVolumeDriverComponent::IsDriverNeeded(UTDFluidPresetAsset::BuildVolumeDriverForPresetKind(Kind), UTDFluidPresetAsset::BuildTerrainHeightForPresetKind(Kind), nullptr);
		TestTrue(*FString::Printf(TEXT("Heterogeneous stage kind %d needs a driver"), static_cast<int32>(Kind)), bIsNeeded);
	}
	TestTrue(TEXT("Floor smoke uses scene capture"), UTDFluidPresetAsset::BuildTerrainHeightForPresetKind(ETDFluidPresetKind::FloorSmokeCapture).VolumeHeightSource == ETDFluidVolumeHeightSource::SceneCapture);
	Preset->TertiaryOutputActorTag = TargetTag;
	TestTrue(TEXT("A slot tag alone needs a driver"), UTDFluidVolumeDriverComponent::IsDriverNeeded(FTDFluidVolumeDriverSettings(), FTDFluidTerrainHeightSettings(), Preset));
	TestFalse(TEXT("Fire family excludes new kinds"), UTDFluidPresetAsset::IsFireFamilyPresetKind(ETDFluidPresetKind::VortexHeterogeneous));
	TestTrue(TEXT("Fire family keeps PortalVortex"), UTDFluidPresetAsset::IsFireFamilyPresetKind(ETDFluidPresetKind::PortalVortex));
	TestFalse(TEXT("Fire family excludes TileWaves"), UTDFluidPresetAsset::IsFireFamilyPresetKind(ETDFluidPresetKind::TileWaves));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeDriverRecollectAndRecaptureTest,
	"TDGame.FluidFX.VolumeDriver.RecollectAndRecapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeDriverRecollectAndRecaptureTest::RunTest(const FString& Parameters)
{
	FTDFluidOutputTargetSettings Settings;
	Settings.RecollectIntervalSeconds = 1.0f;
	TestTrue(TEXT("Forced recollect always runs"), UTDFluidVolumeDriverComponent::ShouldRecollectTargets(0.1, 0.0, Settings, true));
	TestFalse(TEXT("Recollect waits for the interval"), UTDFluidVolumeDriverComponent::ShouldRecollectTargets(0.5, 0.0, Settings, false));
	TestTrue(TEXT("Recollect runs after the interval"), UTDFluidVolumeDriverComponent::ShouldRecollectTargets(1.1, 0.0, Settings, false));
	Settings.bRecollectTargetsDuringGame = false;
	TestFalse(TEXT("Disabled recollect never runs unforced"), UTDFluidVolumeDriverComponent::ShouldRecollectTargets(100.0, 0.0, Settings, false));

	const FVector2D Center(500.0, 250.0);
	int32 RecaptureCount = 0;
	for (int32 TickIndex = 0; TickIndex < 120; ++TickIndex)
	{
		RecaptureCount += UTDFluidVolumeDriverComponent::ShouldRecaptureHeight(Center, Center, 10.0f, 4, false) ? 1 : 0;
	}
	TestEqual(TEXT("A still window never recaptures"), RecaptureCount, 0);
	TestFalse(TEXT("Small moves do not recapture"), UTDFluidVolumeDriverComponent::ShouldRecaptureHeight(Center + FVector2D(39.0, 0.0), Center, 10.0f, 4, false));
	TestTrue(TEXT("Moving cells x cell size recaptures"), UTDFluidVolumeDriverComponent::ShouldRecaptureHeight(Center + FVector2D(40.0, 0.0), Center, 10.0f, 4, false));
	TestTrue(TEXT("Forced recapture runs"), UTDFluidVolumeDriverComponent::ShouldRecaptureHeight(Center, Center, 10.0f, 4, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidVolumeDriverTaggedTargetCollectionTest,
	"TDGame.FluidFX.VolumeDriver.TaggedTargetCollection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidVolumeDriverTaggedTargetCollectionTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidVolumeDriverTestPrivate;
	FDriverTestWorld TestWorld;
	UWorld& World = *TestWorld.World;
	UMaterialInterface* SurfaceMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	UMaterialInterface* OriginalMaterial = UMaterial::GetDefaultMaterial(MD_DeferredDecal);

	UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(GetTransientPackage(), ETDFluidPresetKind::Smoke);
	Preset->PrimaryOutputMaterials = { TSoftObjectPtr<UMaterialInterface>(SurfaceMaterial) };
	Preset->PrimaryOutputActorTag = TargetTag;

	AActor* Host = World.SpawnActor<AActor>();
	UTDFluidLiveComponent* LiveComponent = NewObject<UTDFluidLiveComponent>(Host);
	LiveComponent->ActivePreset = Preset;
	UTDFluidVolumeDriverComponent* Driver = NewObject<UTDFluidVolumeDriverComponent>(Host);
	Driver->RegisterComponent();
	Driver->ConfigureFromLiveComponent(*LiveComponent);
	UMaterialInstanceDynamic* SlotMID = Driver->GetOutputMaterialInstance(ETDFluidOutputMaterialSlot::Primary);
	TestNotNull(TEXT("Loaded slot material makes a slot MID"), SlotMID);

	TArray<UStaticMeshComponent*> Meshes;
	for (int32 MeshIndex = 0; MeshIndex < 3; ++MeshIndex)
	{
		Meshes.Add(SpawnTaggedMeshActor(World, OriginalMaterial));
	}
	TestEqual(TEXT("First collection finds three tagged actors"), Driver->CollectTaggedTargets(0.0, false), 3);
	Meshes.Add(SpawnTaggedMeshActor(World, OriginalMaterial));
	TestEqual(TEXT("Collection waits for the interval"), Driver->CollectTaggedTargets(0.5, false), 0);
	TestEqual(TEXT("Recollection after 1.1 s finds the new actor"), Driver->CollectTaggedTargets(1.1, false), 1);
	TestEqual(TEXT("Four targets are driven"), Driver->GetDrivenTargetCount(), 4);
	for (const UStaticMeshComponent* Mesh : Meshes)
	{
		TestTrue(TEXT("Tagged mesh uses the slot MID"), Mesh->GetMaterial(0) == SlotMID);
	}

	Driver->RestoreTargetMaterials();
	for (const UStaticMeshComponent* Mesh : Meshes)
	{
		TestTrue(TEXT("Original material is restored"), Mesh->GetMaterial(0) == OriginalMaterial);
	}
	Driver->DestroyComponent();
	TestEqual(TEXT("Destroying the driver releases the render profile"), FTDFluidRenderProfile::GetActiveDriverCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRenderProfileApplyAndRestoreTest,
	"TDGame.FluidFX.RenderProfile.ApplyAndRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRenderProfileApplyAndRestoreTest::RunTest(const FString& Parameters)
{
	for (const FTDFluidRenderProfileCVarBinding& Binding : FTDFluidRenderProfile::GetCVarBindings())
	{
		TestNotNull(*FString::Printf(TEXT("%s maps to registered %s"), Binding.FieldName, Binding.CVarName), IConsoleManager::Get().FindConsoleVariable(Binding.CVarName));
		TestNotNull(*FString::Printf(TEXT("%s is a profile field"), Binding.FieldName), FTDFluidHeterogeneousVolumeRenderProfile::StaticStruct()->FindPropertyByName(FName(Binding.FieldName)));
	}
	TestEqual(TEXT("Ten bindings"), FTDFluidRenderProfile::GetCVarBindings().Num(), 10);

	IConsoleVariable* StepCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HeterogeneousVolumes.MaxStepCount"));
	IConsoleVariable* ResolutionZCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HeterogeneousVolumes.VolumeResolution.Z"));
	if (!StepCVar || !ResolutionZCVar)
	{
		return false;
	}
	const FString OriginalStep = StepCVar->GetString();
	const FString OriginalResolutionZ = ResolutionZCVar->GetString();
	const uint32 OriginalStepSetBy = StepCVar->GetFlags() & ECVF_SetByMask;

	FTDFluidRenderProfile::AcquireDriver();
	FTDFluidHeterogeneousVolumeRenderProfile Profile;
	Profile.MaxStepCount = 256;
	Profile.VolumeResolutionZ = 256;
	FTDFluidRenderProfile::ApplyProfile(Profile);
	TestEqual(TEXT("Step count is applied"), StepCVar->GetInt(), 256);
	TestEqual(TEXT("Resolution Z is applied"), ResolutionZCVar->GetInt(), 256);
	TestEqual(TEXT("Two variables are overridden"), FTDFluidRenderProfile::GetOverriddenCVarCount(), 2);

	Profile.MaxStepCount = -1;
	Profile.VolumeResolutionZ = -1;
	FTDFluidRenderProfile::ApplyProfile(Profile);
	TestEqual(TEXT("Minus one restores step count"), StepCVar->GetString(), OriginalStep);
	TestEqual(TEXT("Minus one restores resolution Z"), ResolutionZCVar->GetString(), OriginalResolutionZ);
	TestEqual(TEXT("Nothing stays overridden"), FTDFluidRenderProfile::GetOverriddenCVarCount(), 0);
	AddInfo(FString::Printf(TEXT("Step count SetBy after restore 0x%08x, original 0x%08x"), StepCVar->GetFlags() & ECVF_SetByMask, OriginalStepSetBy));

	Profile.MaxStepCount = 128;
	FTDFluidRenderProfile::ApplyProfile(Profile);
	TestEqual(TEXT("Step count is applied again"), StepCVar->GetInt(), 128);
	FTDFluidRenderProfile::ReleaseDriver();
	TestEqual(TEXT("No drivers restore everything"), StepCVar->GetString(), OriginalStep);
	TestEqual(TEXT("No drivers leave no overrides"), FTDFluidRenderProfile::GetOverriddenCVarCount(), 0);
	TestEqual(TEXT("No active drivers"), FTDFluidRenderProfile::GetActiveDriverCount(), 0);
	return true;
}

#endif
