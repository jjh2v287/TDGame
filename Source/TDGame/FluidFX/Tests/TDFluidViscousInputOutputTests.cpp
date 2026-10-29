#if WITH_DEV_AUTOMATION_TESTS

#include "Components/DecalComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidInputBrushRules.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidSurfaceOutputActor.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace TDFluidViscousInputOutputTestPrivate
{
	constexpr int32 OutputTestGridResolution = 32;
	constexpr double OutputTestExtentCm = 800.0;
	const ETDFluidBrushSource BrushSources[] = { ETDFluidBrushSource::Other, ETDFluidBrushSource::PointArray, ETDFluidBrushSource::DataChannel, ETDFluidBrushSource::Fragment };

	bool AreFloatsBitIdentical(float A, float B)
	{
		return FMemory::Memcmp(&A, &B, sizeof(float)) == 0;
	}

	bool AreStampsBitIdentical(const FTDFluidBrushStamp& A, const FTDFluidBrushStamp& B)
	{
		return A.WorldLocation == B.WorldLocation && A.WorldVelocity == B.WorldVelocity
			&& AreFloatsBitIdentical(A.RadiusCm, B.RadiusCm) && AreFloatsBitIdentical(A.DensityStrength, B.DensityStrength)
			&& AreFloatsBitIdentical(A.VelocityStrength, B.VelocityStrength) && AreFloatsBitIdentical(A.RadialVelocityCmPerSec, B.RadialVelocityCmPerSec)
			&& AreFloatsBitIdentical(A.HeatStrength, B.HeatStrength) && AreFloatsBitIdentical(A.RippleImpulseCm, B.RippleImpulseCm)
			&& A.Channel == B.Channel && A.Source == B.Source;
	}

	TArray<FTDFluidBrushStamp> MakeBrushStamps()
	{
		TArray<FTDFluidBrushStamp> Stamps;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			FTDFluidBrushStamp Stamp;
			Stamp.WorldLocation = FVector(37.0 * Index, -21.0 * Index, 5.0);
			Stamp.WorldVelocity = FVector(120.0 + 15.0 * Index, -40.0, 0.0);
			Stamp.RadiusCm = 30.0f + Index;
			Stamp.DensityStrength = 0.25f + 0.1f * Index;
			Stamp.VelocityStrength = 0.3f + 0.17f * Index;
			Stamp.RadialVelocityCmPerSec = 10.0f * Index;
			Stamp.HeatStrength = 0.05f * Index;
			Stamp.Source = BrushSources[Index % UE_ARRAY_COUNT(BrushSources)];
			Stamps.Add(Stamp);
		}
		return Stamps;
	}

	struct FTDFluidViscousOutputTestWorld
	{
		FTDFluidViscousOutputTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidViscousOutputTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidViscousOutputTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		UWorld* World = nullptr;
	};

	ATDFluidLiveActor* SpawnSourceActor(UWorld* World, ETDFluidPresetKind Kind, const FVector& Location)
	{
		ATDFluidLiveActor* LiveActor = World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!LiveActor)
		{
			return nullptr;
		}

		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(World, Kind);
		Preset->bOverridePresetDefaults = true;
		Preset->SimulationConfig.GridResolution = OutputTestGridResolution;
		Preset->SimulationConfig.SimExtentCm = FVector2D(OutputTestExtentCm, OutputTestExtentCm);
		Preset->SimulationConfig.SolverBackend = ETDFluidSolverBackend::Cpu;
		LiveActor->InitializeFromPreset(Preset, 0.0f);
		return LiveActor;
	}

	struct FTDFluidSurfaceDecalConsoleScope
	{
		FTDFluidSurfaceDecalConsoleScope()
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("TD.FluidFX.SurfaceDecal"));
			if (Variable)
			{
				PreviousValue = Variable->GetInt();
				Variable->Set(-1, ECVF_SetByCode);
			}
		}

		~FTDFluidSurfaceDecalConsoleScope()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = -1;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidViscousInputBrushVelocityScaleTest,
	"TDGame.FluidFX.Viscous.Input.BrushVelocityScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidViscousInputBrushVelocityScaleTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidViscousInputOutputTestPrivate;

	const FTDFluidPlaneBasis WorldBasis = FTDFluidPlaneBasis::MakeWorldFacing();
	const TArray<FTDFluidBrushStamp> OriginalStamps = MakeBrushStamps();

	FTDFluidInputBrushSettings NeutralSettings;
	NeutralSettings.BrushVelocityScale = 1.0f;
	TestFalse(TEXT("A unit brush velocity scale keeps the rules inactive"), FTDFluidInputBrushRules::HasActiveRules(NeutralSettings));
	TArray<FTDFluidBrushStamp> NeutralStamps = OriginalStamps;
	FTDFluidInputBrushRules::ApplyToStamps(NeutralStamps, NeutralSettings, WorldBasis, 0.0);
	int32 NeutralChangedCount = 0;
	for (int32 Index = 0; Index < OriginalStamps.Num(); ++Index)
	{
		NeutralChangedCount += NeutralStamps.IsValidIndex(Index) && AreStampsBitIdentical(NeutralStamps[Index], OriginalStamps[Index]) ? 0 : 1;
	}
	TestEqual(TEXT("A unit brush velocity scale leaves stamps bit identical"), NeutralChangedCount, 0);

	FTDFluidInputBrushSettings ScaledSettings;
	ScaledSettings.BrushVelocityScale = 2.0f;
	TestTrue(TEXT("A non-unit brush velocity scale activates the rules"), FTDFluidInputBrushRules::HasActiveRules(ScaledSettings));
	TArray<FTDFluidBrushStamp> ScaledStamps = OriginalStamps;
	FTDFluidInputBrushRules::ApplyToStamps(ScaledStamps, ScaledSettings, WorldBasis, 0.0);
	if (!TestEqual(TEXT("Scaling keeps every stamp"), ScaledStamps.Num(), OriginalStamps.Num()))
	{
		return false;
	}

	int32 WrongVelocityCount = 0;
	int32 ChangedOtherFieldCount = 0;
	for (int32 Index = 0; Index < OriginalStamps.Num(); ++Index)
	{
		const FTDFluidBrushStamp& Original = OriginalStamps[Index];
		FTDFluidBrushStamp Scaled = ScaledStamps[Index];
		WrongVelocityCount += AreFloatsBitIdentical(Scaled.VelocityStrength, Original.VelocityStrength * 2.0f) ? 0 : 1;
		Scaled.VelocityStrength = Original.VelocityStrength;
		ChangedOtherFieldCount += AreStampsBitIdentical(Scaled, Original) ? 0 : 1;
	}
	TestEqual(TEXT("Scale two doubles every velocity strength"), WrongVelocityCount, 0);
	TestEqual(TEXT("Scale two leaves the other stamp fields unchanged"), ChangedOtherFieldCount, 0);
	TestTrue(TEXT("The scaled stamps differ from the defaults"), !AreFloatsBitIdentical(ScaledStamps[1].VelocityStrength, OriginalStamps[1].VelocityStrength));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidViscousSurfaceOutputPlacementAndGateTest,
	"TDGame.FluidFX.Viscous.SurfaceOutput.PlacementAndGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidViscousSurfaceOutputPlacementAndGateTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidViscousInputOutputTestPrivate;

	FVector PlacedLocation;
	FVector PlacedSize;
	ATDFluidSurfaceOutputActor::ComputeDecalPlacement(FVector2D(100.0, -200.0), FVector2D(1280.0, 640.0), 50.0, 400.0f, PlacedLocation, PlacedSize);
	TestTrue(TEXT("Decal sits at the window center and floor height"), PlacedLocation.Equals(FVector(100.0, -200.0, 50.0), 1.0e-6));
	TestTrue(TEXT("Decal size is half box height and swapped half extents"), PlacedSize.Equals(FVector(200.0, 320.0, 640.0), 1.0e-6));

	const bool PresetStates[] = { false, true };
	const int32 ConsoleStates[] = { -1, 0, 1 };
	for (const bool bIsEnabledByPreset : PresetStates)
	{
		for (const int32 ConsoleOverride : ConsoleStates)
		{
			const bool bExpected = ConsoleOverride == 0 ? false : (ConsoleOverride > 0 ? true : bIsEnabledByPreset);
			TestEqual(*FString::Printf(TEXT("Decal gate preset %d console %d"), bIsEnabledByPreset ? 1 : 0, ConsoleOverride), ATDFluidSurfaceOutputActor::ResolveSurfaceDecalEnabled(bIsEnabledByPreset, ConsoleOverride), bExpected);
		}
	}

	UMaterialInterface* DecalTestMaterial = UMaterial::GetDefaultMaterial(MD_DeferredDecal);
	if (!TestNotNull(TEXT("Engine default decal material"), DecalTestMaterial))
	{
		return false;
	}

	FTDFluidSurfaceDecalConsoleScope ConsoleScope;
	FTDFluidViscousOutputTestWorld TestWorld;
	const FVector SourceLocation(300.0, -150.0, 75.0);
	ATDFluidLiveActor* BloodSource = SpawnSourceActor(TestWorld.World, ETDFluidPresetKind::ViscousBlood, SourceLocation);
	ATDFluidLiveActor* MudSource = SpawnSourceActor(TestWorld.World, ETDFluidPresetKind::ViscousMud, SourceLocation + FVector(5000.0, 0.0, 0.0));
	if (!TestNotNull(TEXT("Blood source actor"), BloodSource) || !TestNotNull(TEXT("Mud source actor"), MudSource))
	{
		return false;
	}

	ATDFluidSurfaceOutputActor* Unsourced = TestWorld.World->SpawnActor<ATDFluidSurfaceOutputActor>(ATDFluidSurfaceOutputActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	ATDFluidSurfaceOutputActor* BloodOutput = TestWorld.World->SpawnActor<ATDFluidSurfaceOutputActor>(ATDFluidSurfaceOutputActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	ATDFluidSurfaceOutputActor* MudOutput = TestWorld.World->SpawnActor<ATDFluidSurfaceOutputActor>(ATDFluidSurfaceOutputActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Unsourced output"), Unsourced) || !TestNotNull(TEXT("Blood output"), BloodOutput) || !TestNotNull(TEXT("Mud output"), MudOutput))
	{
		return false;
	}

	BloodOutput->SourceActor = BloodSource;
	BloodOutput->UpdateDecal();
	TestFalse(TEXT("No decal before the output material is bound"), BloodOutput->IsDecalShown());
	TestEqual(TEXT("Nothing is registered before binding"), BloodOutput->GetRegisteredOutputMaterialCount(), 0);

	Unsourced->BindOutputAssets(DecalTestMaterial, nullptr, nullptr);
	TestFalse(TEXT("An output without a source hides the decal"), Unsourced->IsDecalShown());
	TestEqual(TEXT("An output without a source registers nothing"), Unsourced->GetRegisteredOutputMaterialCount(), 0);

	BloodOutput->BindOutputAssets(DecalTestMaterial, nullptr, nullptr);
	TestEqual(TEXT("The decal material is registered once"), BloodOutput->GetRegisteredOutputMaterialCount(), 1);
	TestTrue(TEXT("The decal shows for a preset with decal output"), BloodOutput->IsDecalShown());
	TestFalse(TEXT("No output mesh without a mesh asset"), BloodOutput->IsMeshShown());
	BloodOutput->BindOutputAssets(DecalTestMaterial, nullptr, nullptr);
	TestEqual(TEXT("Binding again does not register twice"), BloodOutput->GetRegisteredOutputMaterialCount(), 1);

	const UTDFluidLiveComponent& BloodComponent = *BloodSource->LiveComponent;
	float TerrainMinimumCm = 0.0f;
	float TerrainMaximumCm = 0.0f;
	const bool bHasTerrainRange = BloodComponent.TryGetSampledTerrainRangeCm(TerrainMinimumCm, TerrainMaximumCm);
	const double ExpectedFloorCm = bHasTerrainRange ? 0.5 * (static_cast<double>(TerrainMinimumCm) + static_cast<double>(TerrainMaximumCm)) : SourceLocation.Z;
	const float MarginCm = BloodComponent.GetSurfaceRenderSettings().DecalBoxHeightCm;
	const float ExpectedBoxCm = bHasTerrainRange ? (TerrainMaximumCm - TerrainMinimumCm) + MarginCm : MarginCm;
	FVector ExpectedLocation;
	FVector ExpectedSize;
	ATDFluidSurfaceOutputActor::ComputeDecalPlacement(BloodComponent.GetSimulationCenterWorld(), BloodComponent.GetSimulationExtentCm(), ExpectedFloorCm, ExpectedBoxCm, ExpectedLocation, ExpectedSize);
	TestTrue(TEXT("The decal follows the source window"), BloodOutput->OutputDecal->GetComponentLocation().Equals(ExpectedLocation, 0.01));
	TestTrue(TEXT("The decal size covers the source window"), BloodOutput->OutputDecal->DecalSize.Equals(ExpectedSize, 0.01));
	TestTrue(TEXT("The decal projects downward"), BloodOutput->OutputDecal->GetComponentQuat().Equals(FRotator(-90.0, 0.0, 0.0).Quaternion(), 0.0001));
	TestEqual(TEXT("The decal uses the configured sort order"), BloodOutput->OutputDecal->SortOrder, BloodOutput->DecalSortOrder);

	BloodSource->LiveComponent->SetSimulationAwake(false);
	BloodOutput->UpdateDecal();
	TestFalse(TEXT("A sleeping source hides the decal"), BloodOutput->IsDecalShown());
	BloodSource->LiveComponent->SetSimulationAwake(true);
	BloodOutput->UpdateDecal();
	TestTrue(TEXT("A woken source shows the decal again"), BloodOutput->IsDecalShown());

	BloodSource->LiveComponent->SetPlaneBasis(FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation::CameraFacing, FRotator(-30.0, 40.0, 0.0), 0.0f), SourceLocation, 0.0);
	BloodOutput->UpdateDecal();
	TestFalse(TEXT("A camera facing source hides the decal"), BloodOutput->IsDecalShown());

	MudOutput->SourceActor = MudSource;
	MudOutput->BindOutputAssets(DecalTestMaterial, nullptr, nullptr);
	TestEqual(TEXT("The mud output registers its decal material"), MudOutput->GetRegisteredOutputMaterialCount(), 1);
	TestFalse(TEXT("A preset without decal output hides the decal"), MudOutput->IsDecalShown());
	if (ConsoleScope.Variable)
	{
		ConsoleScope.Variable->Set(1, ECVF_SetByCode);
		MudOutput->UpdateDecal();
		TestTrue(TEXT("The console override forces the decal on"), MudOutput->IsDecalShown());
		ConsoleScope.Variable->Set(0, ECVF_SetByCode);
		MudOutput->UpdateDecal();
		TestFalse(TEXT("The console override hides the decal"), MudOutput->IsDecalShown());
		ConsoleScope.Variable->Set(-1, ECVF_SetByCode);
	}
	return true;
}

#endif
