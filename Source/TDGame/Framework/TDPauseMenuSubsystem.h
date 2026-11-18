#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Widgets/SWidget.h"
#include "TDPauseMenuSubsystem.generated.h"

class APlayerController;
class UGameViewportClient;

UCLASS()
class TDGAME_API UTDPauseMenuSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static const TCHAR* GetSaveSlotName();

	virtual void Deinitialize() override;

	bool OpenMenu(APlayerController& Controller);
	bool CloseMenu(APlayerController& Controller);
	bool ToggleMenu(APlayerController& Controller);
	bool SaveWorldState() const;
	bool ClearStateForNewGame() const;
	void SaveAndQuit();
	void StartNewGame();

	bool IsMenuOpen() const { return bIsMenuOpen; }
	bool IsMenuWidgetShown() const { return MenuWidget.IsValid(); }

private:
	void ShowMenuWidget(APlayerController& Controller);
	void HideMenuWidget();
	void HandleContinueClicked();

	void SaveViewportInputState(UGameViewportClient& Viewport);
	void RestoreViewportInputState();

	TSharedPtr<SWidget> MenuWidget;
	TWeakObjectPtr<UGameViewportClient> SavedInputViewport;
	EMouseCaptureMode SavedMouseCaptureMode = EMouseCaptureMode::CapturePermanently;
	EMouseLockMode SavedMouseLockMode = EMouseLockMode::DoNotLock;
	bool bSavedHideCursorDuringCapture = false;
	bool bSavedIgnoreInput = false;
	TWeakObjectPtr<UGameViewportClient> MenuViewport;
	TWeakObjectPtr<APlayerController> MenuController;
	bool bIsMenuOpen = false;
};
