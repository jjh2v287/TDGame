#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Tests/TDPieWaitUntilCommand.h"
#include "World/Persistence/TDPersistentActor.h"
#include "World/Persistence/TDPersistentStateComponent.h"
#include "World/Persistence/TDPersistentTestChest.h"
#include "World/Streaming/TDSeamlessTravelSubsystem.h"
#include "World/Persistence/TDWorldStateSubsystem.h"

namespace
{
	const FName TDPieTestDungeonId(TEXT("MainCrypt"));
	const FName TDChestOpenedStateKey(TEXT("Opened"));
	const FVector TDFallbackChestLocation(-10300.0, 2600.0, 630.0);
	const FVector TDChestOffsetFromPawn(300.0, 0.0, 0.0);
	constexpr float TDPieWarmupSeconds = 10.0f;
	constexpr double TDTravelTimeoutSeconds = 60.0;

	struct FTDChestStatePieTestState
	{
		bool bIsAborted = false;
		FGuid ChestStableId;
		FVector ChestLocation = TDFallbackChestLocation;
		TWeakObjectPtr<UTDSeamlessTravelSubsystem> Travel;
		TWeakObjectPtr<ATDPersistentTestChest> FirstChest;

		bool IsTravelIdle() const
		{
			return !Travel.IsValid() || Travel->GetTravelState() == ETDSeamlessTravelState::Idle;
		}
	};

	ATDPersistentTestChest* SpawnChestWithStableId(UWorld* World, const FVector& Location, const FGuid& StableId)
	{
		ATDPersistentTestChest* Chest = World->SpawnActorDeferred<ATDPersistentTestChest>(ATDPersistentTestChest::StaticClass(), FTransform(Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Chest)
		{
			return nullptr;
		}
		Chest->GetPersistentStateComponent()->StableId = StableId;
		Chest->FinishSpawning(FTransform(Location));
		return Chest;
	}

	UTDWorldStateSubsystem* FindWorldState(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UTDWorldStateSubsystem>() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDChestStateSurvivesStreamingPieTest, "TDGame.WorldState.ChestStateSurvivesStreaming", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTDChestStateSurvivesStreamingPieTest::RunTest(const FString& Parameters)
{
	using namespace TDPieTestUtils;

	TSharedRef<FTDChestStatePieTestState> State = MakeShared<FTDChestStatePieTestState>();
	if (!OpenMapAndStartPie(*this, OpenWorldMapPackagePath))
	{
		return false;
	}

	const auto Abort = [State]() { State->bIsAborted = true; };
	const auto WaitForTravelIdle = [this, State, Abort](const FString& Description)
	{
		EnqueueWaitUntil(*this, Description, [State]() { return State->bIsAborted || State->IsTravelIdle(); }, TDTravelTimeoutSeconds, Abort);
	};

	EnqueueWaitSeconds(TDPieWarmupSeconds);

	EnqueueStep([this, State]()
	{
		UWorld* World = GetPieWorld();
		UTDSeamlessTravelSubsystem* Travel = World ? World->GetSubsystem<UTDSeamlessTravelSubsystem>() : nullptr;
		UTDWorldStateSubsystem* WorldState = FindWorldState(World);
		if (!TestNotNull(TEXT("PIE world has a seamless travel subsystem"), Travel) || !TestNotNull(TEXT("PIE game instance has a world state subsystem"), WorldState))
		{
			State->bIsAborted = true;
			return true;
		}
		State->Travel = Travel;

		if (APawn* Pawn = FindPlayerPawn(World))
		{
			State->ChestLocation = Pawn->GetActorLocation() + TDChestOffsetFromPawn;
		}
		State->ChestStableId = FGuid::NewGuid();

		ATDPersistentTestChest* Chest = SpawnChestWithStableId(World, State->ChestLocation, State->ChestStableId);
		if (!TestNotNull(TEXT("First chest spawned"), Chest))
		{
			State->bIsAborted = true;
			return true;
		}
		State->FirstChest = Chest;
		TestEqual(TEXT("First chest keeps the test StableId after spawn"), Chest->GetPersistentStateComponent()->StableId, State->ChestStableId);
		TestFalse(TEXT("First chest starts closed"), Chest->bIsOpened);

		Chest->Open();
		TestTrue(TEXT("First chest is opened"), Chest->bIsOpened);
		TestTrue(TEXT("Chest state is captured into the world state subsystem"), Chest->GetPersistentStateComponent()->CaptureOwnerState());

		FTDActorStateRecord Record;
		if (TestTrue(TEXT("World state has a record for the chest StableId"), WorldState->FindActorRecord(State->ChestStableId, Record)))
		{
			TestTrue(TEXT("Stored record marks the chest as opened"), Record.GetBool(TDChestOpenedStateKey, false));
		}

		if (!TestTrue(TEXT("RequestTravel to dungeon entry is accepted"), Travel->RequestTravel(TDPieTestDungeonId, true)))
		{
			State->bIsAborted = true;
		}
		return true;
	});

	WaitForTravelIdle(TEXT("Travel to dungeon entry returns to Idle"));

	EnqueueStep([this, State]()
	{
		if (State->bIsAborted)
		{
			return true;
		}

		if (State->FirstChest.IsValid())
		{
			AddInfo(TEXT("Runtime-spawned chest was not unloaded by world partition streaming; destroying it to simulate cell unload."));
			State->FirstChest->Destroy();
		}
		else
		{
			AddInfo(TEXT("Chest was unloaded while the player was in the dungeon."));
		}

		UWorld* World = GetPieWorld();
		UTDWorldStateSubsystem* WorldState = FindWorldState(World);
		FTDActorStateRecord Record;
		if (TestNotNull(TEXT("World state subsystem still exists in the dungeon"), WorldState) && TestTrue(TEXT("Chest record survives after unload"), WorldState->FindActorRecord(State->ChestStableId, Record)))
		{
			TestTrue(TEXT("Chest record still marks the chest as opened after unload"), Record.GetBool(TDChestOpenedStateKey, false));
		}

		if (!State->Travel.IsValid() || !TestTrue(TEXT("RequestTravel back to field is accepted"), State->Travel->RequestTravel(TDPieTestDungeonId, false)))
		{
			State->bIsAborted = true;
		}
		return true;
	});

	WaitForTravelIdle(TEXT("Travel back to field returns to Idle"));

	EnqueueStep([this, State]()
	{
		if (State->bIsAborted)
		{
			return true;
		}

		UWorld* World = GetPieWorld();
		if (!TestNotNull(TEXT("PIE world exists after returning to field"), World))
		{
			return true;
		}

		ATDPersistentTestChest* RespawnedChest = SpawnChestWithStableId(World, State->ChestLocation, State->ChestStableId);
		if (!TestNotNull(TEXT("Second chest spawned with the same StableId"), RespawnedChest))
		{
			return true;
		}
		TestTrue(TEXT("Second chest has begun play"), RespawnedChest->HasActorBegunPlay());
		TestTrue(TEXT("Second chest restores the opened state from the world state subsystem"), RespawnedChest->bIsOpened);
		RespawnedChest->Destroy();
		return true;
	});

	EnqueueEndPie();
	return true;
}

#endif
