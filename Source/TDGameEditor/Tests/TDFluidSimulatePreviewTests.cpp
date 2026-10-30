#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidSimulatePreviewSubsystem.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace TDFluidSimulatePreviewTestPrivate
{
	constexpr float SimulatePreviewActivationDistanceCm = 1000.0f;

	struct FTDFluidSimulatePreviewTestWorld
	{
		FTDFluidSimulatePreviewTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidSimulatePreviewTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidSimulatePreviewTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		ATDFluidLiveActor* SpawnSleepingMudActor(const FVector& Location, bool bUsesActivationVolume) const
		{
			UTDFluidLiveSubsystem* FluidSubsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
			UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(World, ETDFluidPresetKind::Mud);
			ATDFluidLiveActor* LiveActor = FluidSubsystem ? FluidSubsystem->SpawnPooledFluidActor(Preset, Location, 0.0f) : nullptr;
			if (!LiveActor || !LiveActor->LiveComponent)
			{
				return nullptr;
			}

			LiveActor->SetActorLocation(Location);
			LiveActor->bShouldUseActivationVolume = bUsesActivationVolume;
			LiveActor->ActivationDistanceCm = SimulatePreviewActivationDistanceCm;
			LiveActor->LiveComponent->SetSimulationAwake(false);
			return LiveActor;
		}

		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSimulatePreviewActivationTest,
	"TDGameEditor.FluidFX.SimulatePreview.Activation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSimulatePreviewActivationTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSimulatePreviewTestPrivate;

	TestFalse(TEXT("Simulate preview is off by default"), UTDFluidSimulatePreviewSubsystem::IsPreviewConsoleEnabled());

	FTDFluidSimulatePreviewTestWorld TestWorld;
	ATDFluidLiveActor* NearActor = TestWorld.SpawnSleepingMudActor(FVector(200.0, 0.0, 0.0), true);
	ATDFluidLiveActor* FarActor = TestWorld.SpawnSleepingMudActor(FVector(SimulatePreviewActivationDistanceCm * 20.0f, 0.0, 0.0), true);
	ATDFluidLiveActor* VolumeOffActor = TestWorld.SpawnSleepingMudActor(FVector(0.0, 200.0, 0.0), false);
	ATDFluidLiveActor* PooledActor = TestWorld.SpawnSleepingMudActor(FVector(0.0, -200.0, 0.0), true);
	if (!TestNotNull(TEXT("Near actor spawns"), NearActor) || !TestNotNull(TEXT("Far actor spawns"), FarActor) || !TestNotNull(TEXT("Volume off actor spawns"), VolumeOffActor) || !TestNotNull(TEXT("Pooled actor spawns"), PooledActor))
	{
		return false;
	}

	TestTrue(TEXT("Spawned actors are distinct"), NearActor != FarActor && NearActor != VolumeOffActor && NearActor != PooledActor && FarActor != VolumeOffActor && FarActor != PooledActor && VolumeOffActor != PooledActor);
	PooledActor->SetPooledInactive(true);
	const bool bPooledAwakeBefore = PooledActor->LiveComponent->IsSimulationAwake();

	const int32 AwakeCount = UTDFluidSimulatePreviewSubsystem::EvaluatePreviewActivation(*TestWorld.World, FVector::ZeroVector);
	TestEqual(TEXT("Only the near actor is woken"), AwakeCount, 1);
	TestTrue(TEXT("Actor inside the view radius wakes"), NearActor->LiveComponent->IsSimulationAwake());
	TestFalse(TEXT("Actor outside the view radius stays asleep"), FarActor->LiveComponent->IsSimulationAwake());
	TestFalse(TEXT("Actor without the activation volume is unchanged"), VolumeOffActor->LiveComponent->IsSimulationAwake());
	TestEqual(TEXT("Pooled inactive actor is unchanged"), PooledActor->LiveComponent->IsSimulationAwake(), bPooledAwakeBefore);

	const int32 FarAwayCount = UTDFluidSimulatePreviewSubsystem::EvaluatePreviewActivation(*TestWorld.World, FVector(SimulatePreviewActivationDistanceCm * 40.0f, 0.0, 0.0));
	TestEqual(TEXT("A far view wakes nothing"), FarAwayCount, 0);
	TestFalse(TEXT("Near actor sleeps once the view leaves"), NearActor->LiveComponent->IsSimulationAwake());
	return true;
}

#endif
