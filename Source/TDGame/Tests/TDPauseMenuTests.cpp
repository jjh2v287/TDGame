#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/TDPauseMenuSubsystem.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "CoreGlobals.h"
#include "TimerManager.h"
#include "World/Persistence/TDWorldStateSubsystem.h"

namespace
{
	struct FTDPauseMenuTestWorld
	{
		explicit FTDPauseMenuTestWorld(const bool bUseGameInstance)
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDPauseMenuTestWorld"));
			if (bUseGameInstance)
			{
				GameInstance = NewObject<UGameInstance>(GEngine);
				GameInstance->AddToRoot();
				GameInstance->InitializeStandalone(WorldName, GetTransientPackage());
				World = GameInstance->GetWorld();
			}
			else
			{
				World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
			if (!World)
			{
				return;
			}
			World->InitializeActorsForPlay(FURL());
			World->SetGameMode(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			Controller = World->SpawnActor<APlayerController>();
			if (Controller && !Controller->PlayerState)
			{
				Controller->PlayerState = World->SpawnActor<APlayerState>();
			}
		}

		~FTDPauseMenuTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
			}
			if (GameInstance)
			{
				GameInstance->Shutdown();
				GameInstance->RemoveFromRoot();
			}
			if (!World)
			{
				return;
			}
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		bool IsReady() const
		{
			return World && Controller && Controller->PlayerState && World->GetAuthGameMode();
		}

		UGameInstance* GameInstance = nullptr;
		UWorld* World = nullptr;
		APlayerController* Controller = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPauseMenuOpenCloseTest, "TDGame.Framework.PauseMenu.OpenPausesCloseResumes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPauseMenuOpenCloseTest::RunTest(const FString& Parameters)
{
	FTDPauseMenuTestWorld TestWorld(true);
	if (!TestTrue(TEXT("The test world, game mode and controller exist"), TestWorld.IsReady()))
	{
		return false;
	}

	UTDPauseMenuSubsystem* PauseMenu = TestWorld.World->GetSubsystem<UTDPauseMenuSubsystem>();
	if (!TestNotNull(TEXT("The pause menu subsystem exists"), PauseMenu))
	{
		return false;
	}

	TestFalse(TEXT("The world starts running"), TestWorld.World->IsPaused());
	TestTrue(TEXT("Opening succeeds"), PauseMenu->OpenMenu(*TestWorld.Controller));
	TestTrue(TEXT("The world is paused after opening"), TestWorld.World->IsPaused());
	TestTrue(TEXT("The menu reports open"), PauseMenu->IsMenuOpen());
	TestFalse(TEXT("Opening twice is refused"), PauseMenu->OpenMenu(*TestWorld.Controller));
	TestTrue(TEXT("Closing succeeds"), PauseMenu->CloseMenu(*TestWorld.Controller));
	TestFalse(TEXT("The world resumes after closing"), TestWorld.World->IsPaused());
	TestFalse(TEXT("The menu reports closed"), PauseMenu->IsMenuOpen());
	TestTrue(TEXT("Toggling opens"), PauseMenu->ToggleMenu(*TestWorld.Controller));
	TestTrue(TEXT("Toggle opened the pause"), TestWorld.World->IsPaused());
	TestTrue(TEXT("Toggling closes"), PauseMenu->ToggleMenu(*TestWorld.Controller));
	TestFalse(TEXT("Toggle closed the pause"), TestWorld.World->IsPaused());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPauseMenuTimerFreezeTest, "TDGame.Framework.PauseMenu.TimersFreezeWhilePaused", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPauseMenuTimerFreezeTest::RunTest(const FString& Parameters)
{
	FTDPauseMenuTestWorld TestWorld(true);
	if (!TestTrue(TEXT("The test world, game mode and controller exist"), TestWorld.IsReady()))
	{
		return false;
	}

	UTDPauseMenuSubsystem* PauseMenu = TestWorld.World->GetSubsystem<UTDPauseMenuSubsystem>();
	if (!TestNotNull(TEXT("The pause menu subsystem exists"), PauseMenu))
	{
		return false;
	}

	int32 FireCount = 0;
	FTimerHandle Handle;
	TestWorld.World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([&FireCount]()
	{
		++FireCount;
	}), 0.5f, false);

	++GFrameCounter;
	TestWorld.World->Tick(LEVELTICK_All, 0.2f);
	TestEqual(TEXT("The timer has not fired before the pause"), FireCount, 0);

	const double TimeBeforePause = TestWorld.World->GetTimeSeconds();
	PauseMenu->OpenMenu(*TestWorld.Controller);
	++GFrameCounter;
	TestWorld.World->Tick(LEVELTICK_All, 0.4f);
	++GFrameCounter;
	TestWorld.World->Tick(LEVELTICK_All, 0.4f);
	TestEqual(TEXT("The timer does not fire while paused"), FireCount, 0);
	TestEqual(TEXT("The world clock stands still while paused"), TestWorld.World->GetTimeSeconds(), TimeBeforePause);

	PauseMenu->CloseMenu(*TestWorld.Controller);
	++GFrameCounter;
	TestWorld.World->Tick(LEVELTICK_All, 0.4f);
	++GFrameCounter;
	TestWorld.World->Tick(LEVELTICK_All, 0.4f);
	TestEqual(TEXT("The timer fires after resuming"), FireCount, 1);
	TestTrue(TEXT("The world clock runs again after resuming"), TestWorld.World->GetTimeSeconds() > TimeBeforePause);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDPauseMenuNewGameClearsStateTest, "TDGame.Framework.PauseMenu.NewGameClearsWorldState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDPauseMenuNewGameClearsStateTest::RunTest(const FString& Parameters)
{
	FTDPauseMenuTestWorld TestWorld(true);
	if (!TestTrue(TEXT("The test world, game mode and controller exist"), TestWorld.IsReady()))
	{
		return false;
	}

	UTDPauseMenuSubsystem* PauseMenu = TestWorld.World->GetSubsystem<UTDPauseMenuSubsystem>();
	UTDWorldStateSubsystem* WorldState = TestWorld.GameInstance->GetSubsystem<UTDWorldStateSubsystem>();
	if (!TestNotNull(TEXT("The pause menu subsystem exists"), PauseMenu) || !TestNotNull(TEXT("The world state subsystem exists"), WorldState))
	{
		return false;
	}

	FTDActorStateRecord Record;
	Record.StableId = FGuid::NewGuid();
	WorldState->StoreActorRecord(Record);
	TestEqual(TEXT("A record was stored"), WorldState->GetActorRecordCount(), 1);

	TestTrue(TEXT("Clearing for a new game succeeds"), PauseMenu->ClearStateForNewGame());
	TestEqual(TEXT("The new game cleared every record"), WorldState->GetActorRecordCount(), 0);
	return true;
}

#endif
