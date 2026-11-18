#include "Framework/TDPauseMenuSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/STDPauseMenu.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TDGame.h"
#include "World/Persistence/TDWorldStateSubsystem.h"

namespace
{
	constexpr int32 PauseMenuViewportZOrder = 1000;

	UTDWorldStateSubsystem* FindWorldStateSubsystem(const UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		const UGameInstance* GameInstance = World->GetGameInstance();
		return GameInstance ? GameInstance->GetSubsystem<UTDWorldStateSubsystem>() : nullptr;
	}
}

const TCHAR* UTDPauseMenuSubsystem::GetSaveSlotName()
{
	return TEXT("TDPauseMenuAutoSave");
}

void UTDPauseMenuSubsystem::Deinitialize()
{
	HideMenuWidget();
	bIsMenuOpen = false;
	Super::Deinitialize();
}

bool UTDPauseMenuSubsystem::OpenMenu(APlayerController& Controller)
{
	if (bIsMenuOpen)
	{
		return false;
	}
	if (!Controller.SetPause(true))
	{
		return false;
	}

	bIsMenuOpen = true;
	MenuController = &Controller;
	ShowMenuWidget(Controller);
	return true;
}

bool UTDPauseMenuSubsystem::CloseMenu(APlayerController& Controller)
{
	if (!bIsMenuOpen)
	{
		return false;
	}

	const bool bHadMenuWidget = MenuWidget.IsValid();
	HideMenuWidget();
	bIsMenuOpen = false;
	MenuController.Reset();
	Controller.SetPause(false);
	if (bHadMenuWidget)
	{
		RestoreViewportInputState();
	}
	return true;
}

bool UTDPauseMenuSubsystem::ToggleMenu(APlayerController& Controller)
{
	return bIsMenuOpen ? CloseMenu(Controller) : OpenMenu(Controller);
}

bool UTDPauseMenuSubsystem::SaveWorldState() const
{
	UTDWorldStateSubsystem* WorldState = FindWorldStateSubsystem(GetWorld());
	if (!WorldState)
	{
		return false;
	}
	return WorldState->SaveToSlot(GetSaveSlotName());
}

bool UTDPauseMenuSubsystem::ClearStateForNewGame() const
{
	UTDWorldStateSubsystem* WorldState = FindWorldStateSubsystem(GetWorld());
	if (!WorldState)
	{
		return false;
	}
	WorldState->ClearAllState();
	return true;
}

void UTDPauseMenuSubsystem::SaveAndQuit()
{
	if (!SaveWorldState())
	{
		UE_LOG(LogTDGame, Warning, TEXT("PauseMenu: save failed, staying in the game."));
		return;
	}
	UKismetSystemLibrary::QuitGame(GetWorld(), MenuController.Get(), EQuitPreference::Quit, false);
}

void UTDPauseMenuSubsystem::StartNewGame()
{
	if (!ClearStateForNewGame())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (APlayerController* Controller = MenuController.Get())
	{
		CloseMenu(*Controller);
	}
	UGameplayStatics::OpenLevel(World, FName(*UGameplayStatics::GetCurrentLevelName(World, true)));
}

void UTDPauseMenuSubsystem::ShowMenuWidget(APlayerController& Controller)
{
	UWorld* World = GetWorld();
	UGameViewportClient* GameViewport = World ? World->GetGameViewport() : nullptr;
	if (!FSlateApplication::IsInitialized() || !GameViewport)
	{
		return;
	}

	SaveViewportInputState(*GameViewport);
	MenuViewport = GameViewport;
	MenuWidget = SNew(STDPauseMenu)
		.OnContinueClicked(FSimpleDelegate::CreateUObject(this, &UTDPauseMenuSubsystem::HandleContinueClicked))
		.OnSaveAndQuitClicked(FSimpleDelegate::CreateUObject(this, &UTDPauseMenuSubsystem::SaveAndQuit))
		.OnNewGameClicked(FSimpleDelegate::CreateUObject(this, &UTDPauseMenuSubsystem::StartNewGame));
	GameViewport->AddViewportWidgetContent(MenuWidget.ToSharedRef(), PauseMenuViewportZOrder);
	Controller.SetInputMode(FInputModeGameAndUI().SetWidgetToFocus(MenuWidget).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock).SetHideCursorDuringCapture(false));
}

void UTDPauseMenuSubsystem::SaveViewportInputState(UGameViewportClient& Viewport)
{
	SavedInputViewport = &Viewport;
	SavedMouseCaptureMode = Viewport.GetMouseCaptureMode();
	SavedMouseLockMode = Viewport.GetMouseLockMode();
	bSavedHideCursorDuringCapture = Viewport.HideCursorDuringCapture();
	bSavedIgnoreInput = Viewport.IgnoreInput();
}

void UTDPauseMenuSubsystem::RestoreViewportInputState()
{
	UGameViewportClient* Viewport = SavedInputViewport.Get();
	SavedInputViewport.Reset();
	if (!Viewport || !FSlateApplication::IsInitialized())
	{
		return;
	}

	Viewport->SetMouseCaptureMode(SavedMouseCaptureMode);
	Viewport->SetMouseLockMode(SavedMouseLockMode);
	Viewport->SetHideCursorDuringCapture(bSavedHideCursorDuringCapture);
	Viewport->SetIgnoreInput(bSavedIgnoreInput);
	FSlateApplication::Get().SetUserFocusToGameViewport(0);
}

void UTDPauseMenuSubsystem::HideMenuWidget()
{
	if (MenuWidget.IsValid())
	{
		if (UGameViewportClient* Viewport = MenuViewport.Get())
		{
			Viewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
		}
	}
	MenuWidget.Reset();
	MenuViewport.Reset();
}

void UTDPauseMenuSubsystem::HandleContinueClicked()
{
	if (APlayerController* Controller = MenuController.Get())
	{
		CloseMenu(*Controller);
	}
}
