#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidFoliageFlowActor.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "FluidFX/TDFluidTrackDeformMeshActor.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFluidLookRefreshTestPrivate
{
	struct FTDFluidLookRefreshTestWorld
	{
		FTDFluidLookRefreshTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidLookRefreshTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidLookRefreshTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidLookRefreshSubscribeTest,
	"TDGame.FluidFX.LookRefresh.FoliageAndTrackSubscribe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidLookRefreshSubscribeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidLookRefreshTestPrivate;

	UTDFluidPresetTuningSubsystem* TuningSubsystem = GEngine ? GEngine->GetEngineSubsystem<UTDFluidPresetTuningSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("Preset tuning subsystem exists"), TuningSubsystem))
	{
		return false;
	}

	AddExpectedMessage(TEXT("must all be set"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	FTDFluidLookRefreshTestWorld TestWorld;
	ATDFluidFoliageFlowActor* FoliageActor = TestWorld.World->SpawnActor<ATDFluidFoliageFlowActor>();
	ATDFluidTrackDeformMeshActor* TrackActor = TestWorld.World->SpawnActor<ATDFluidTrackDeformMeshActor>();
	if (!TestNotNull(TEXT("Foliage flow actor spawns"), FoliageActor) || !TestNotNull(TEXT("Track deform actor spawns"), TrackActor))
	{
		return false;
	}

	TestTrue(TEXT("Foliage flow actor subscribes to tuning reloads at BeginPlay"), TuningSubsystem->OnPresetTuningReloaded.IsBoundToObject(FoliageActor));
	TestTrue(TEXT("Track deform actor subscribes to tuning reloads at BeginPlay"), TuningSubsystem->OnPresetTuningReloaded.IsBoundToObject(TrackActor));

	FoliageActor->Destroy();
	TrackActor->Destroy();
	TestFalse(TEXT("Foliage flow actor unsubscribes at EndPlay"), TuningSubsystem->OnPresetTuningReloaded.IsBoundToObject(FoliageActor));
	TestFalse(TEXT("Track deform actor unsubscribes at EndPlay"), TuningSubsystem->OnPresetTuningReloaded.IsBoundToObject(TrackActor));
	return true;
}

#endif
