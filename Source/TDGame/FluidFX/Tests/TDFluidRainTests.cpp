#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidInputBrushRules.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidMassInputTargets.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"

namespace TDFluidRainTestPrivate
{
	struct FTDFluidRainTestWorldScope
	{
		FTDFluidRainTestWorldScope()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidRainTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidRainTestWorldScope()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void TickWorldOnce(float DeltaSeconds)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, DeltaSeconds);
		}

		UWorld* World = nullptr;
	};

	FTDFluidMassInputSettings MakeMassInputSettings(bool bAcceptMassInput)
	{
		FTDFluidMassInputSettings Settings;
		Settings.bAcceptMassInput = bAcceptMassInput;
		return Settings;
	}

	FTDFluidMassInputPoint MakeDefaultFieldPoint()
	{
		FTDFluidMassInputPoint Point;
		Point.WorldLocation = FVector::ZeroVector;
		Point.WorldVelocity = FVector(120.0, -40.0, 0.0);
		Point.RadiusCm = 30.0f;
		Point.DensityStrength = 0.8f;
		Point.HeatStrength = 0.25f;
		return Point;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainMassPointDefaultsKeepFx5Test,
	"TDGame.FluidFX.Rain.MassPointDefaultsKeepFx5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainMassPointDefaultsKeepFx5Test::RunTest(const FString& Parameters)
{
	using namespace TDFluidRainTestPrivate;

	FTDFluidRainTestWorldScope TestWorld;
	UTDFluidLiveSubsystem* Subsystem = TestWorld.World->GetSubsystem<UTDFluidLiveSubsystem>();
	if (!TestNotNull(TEXT("Fluid subsystem exists in the test world"), Subsystem))
	{
		return false;
	}

	UTDFluidPresetAsset* BloodPreset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Blood);
	ATDFluidLiveActor* LiveActor = Subsystem->SpawnPooledFluidActor(BloodPreset, FVector::ZeroVector, 10.0f);
	if (!TestNotNull(TEXT("A fluid actor is spawned"), LiveActor))
	{
		return false;
	}

	TestWorld.TickWorldOnce(0.05f);
	UTDFluidLiveComponent* LiveComponent = LiveActor->LiveComponent;
	if (!TestTrue(TEXT("The CPU solver is initialized"), LiveComponent->IsSimulationInitialized() && !LiveComponent->IsUsingGpuSolver()))
	{
		return false;
	}

	const FTDFluidInputBrushSettings& BrushSettings = LiveComponent->GetInputBrushSettings();
	const FTDFluidMassInputPoint FieldPoint = MakeDefaultFieldPoint();
	LiveComponent->SetMassInputSettingsForTests(MakeMassInputSettings(true));
	const int32 AcceptedStartCount = LiveComponent->GetPendingBrushStamps().Num();
	LiveComponent->QueueMassInputPoints(MakeArrayView(&FieldPoint, 1));
	if (!TestEqual(TEXT("A default field point queues one stamp"), LiveComponent->GetPendingBrushStamps().Num(), AcceptedStartCount + 1))
	{
		return false;
	}

	const FTDFluidBrushStamp FieldStamp = LiveComponent->GetPendingBrushStamps().Last();
	const ETDFluidBrushChannel ExpectedChannel = BloodPreset->DefaultChannel;
	TestTrue(TEXT("Field stamp keeps the point location"), FieldStamp.WorldLocation == FieldPoint.WorldLocation);
	TestTrue(TEXT("Field stamp keeps the point velocity"), FieldStamp.WorldVelocity == FieldPoint.WorldVelocity);
	TestTrue(TEXT("Field stamp radius keeps the FX-5 formula"), FieldStamp.RadiusCm == FMath::Max(FieldPoint.RadiusCm, 0.0f) * BrushSettings.MassPointRadiusScale);
	TestTrue(TEXT("Field stamp density keeps the FX-5 formula"), FieldStamp.DensityStrength == FieldPoint.DensityStrength * BrushSettings.MassPointDensityStrength);
	TestTrue(TEXT("Field stamp velocity strength keeps the FX-5 value"), FieldStamp.VelocityStrength == BrushSettings.MassPointVelocityStrength);
	TestTrue(TEXT("Field stamp heat keeps the FX-5 formula"), FieldStamp.HeatStrength == FieldPoint.HeatStrength * BrushSettings.MassPointHeatStrength);
	TestTrue(TEXT("Field stamp ripple stays zero"), FieldStamp.RippleImpulseCm == 0.0f);
	TestTrue(TEXT("Field stamp uses the preset channel"), FieldStamp.Channel == ExpectedChannel);
	TestTrue(TEXT("Field stamp source stays the data channel"), FieldStamp.Source == ETDFluidBrushSource::DataChannel);

	LiveComponent->SetMassInputSettingsForTests(MakeMassInputSettings(false));
	const int32 RefusedStartCount = LiveComponent->GetPendingBrushStamps().Num();
	LiveComponent->QueueMassInputPoints(MakeArrayView(&FieldPoint, 1));
	TestEqual(TEXT("A data channel point is refused while mass input is off"), LiveComponent->GetPendingBrushStamps().Num(), RefusedStartCount);

	FTDFluidMassInputPoint RainPoint = FieldPoint;
	RainPoint.RippleImpulseCm = 0.5f;
	RainPoint.VelocityStrengthScale = 0.0f;
	LiveComponent->QueueMassInputPoints(MakeArrayView(&RainPoint, 1), ETDFluidBrushSource::Rain);
	if (!TestEqual(TEXT("A rain point enters while mass input is off"), LiveComponent->GetPendingBrushStamps().Num(), RefusedStartCount + 1))
	{
		return false;
	}

	const FTDFluidBrushStamp RainStamp = LiveComponent->GetPendingBrushStamps().Last();
	TestTrue(TEXT("Rain stamp keeps the rain source"), RainStamp.Source == ETDFluidBrushSource::Rain);
	TestTrue(TEXT("Rain stamp velocity strength follows the point scale"), RainStamp.VelocityStrength == BrushSettings.MassPointVelocityStrength * RainPoint.VelocityStrengthScale);
	TestTrue(TEXT("Rain stamp ripple adds the point ripple"), RainStamp.RippleImpulseCm == RainPoint.RippleImpulseCm + BrushSettings.MassPointRippleImpulseCm);
	TestTrue(TEXT("Rain stamp density keeps the FX-5 formula"), RainStamp.DensityStrength == RainPoint.DensityStrength * BrushSettings.MassPointDensityStrength);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidRainVelocityOnlyAllIncludesRainTest,
	"TDGame.FluidFX.Rain.VelocityOnlyAllIncludesRain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidRainVelocityOnlyAllIncludesRainTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("All selection includes rain"), FTDFluidInputBrushRules::IsVelocityOnlySource(ETDFluidVelocityOnlySources::All, ETDFluidBrushSource::Rain));
	TestFalse(TEXT("Data channel selection excludes rain"), FTDFluidInputBrushRules::IsVelocityOnlySource(ETDFluidVelocityOnlySources::DataChannel, ETDFluidBrushSource::Rain));
	return true;
}

#endif
