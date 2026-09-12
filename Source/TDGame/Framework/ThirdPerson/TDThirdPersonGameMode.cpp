#include "Framework/ThirdPerson/TDThirdPersonGameMode.h"

#include "Characters/TDThirdPersonPlayerCharacter.h"
#include "Framework/ThirdPerson/TDThirdPersonPlayerController.h"

ATDThirdPersonGameMode::ATDThirdPersonGameMode()
{
	DefaultPawnClass = ATDThirdPersonPlayerCharacter::StaticClass();
	PlayerControllerClass = ATDThirdPersonPlayerController::StaticClass();
}
