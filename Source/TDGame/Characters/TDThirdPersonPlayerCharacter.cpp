#include "Characters/TDThirdPersonPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"

ATDThirdPersonPlayerCharacter::ATDThirdPersonPlayerCharacter()
{
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->bOrientRotationToMovement = true;
		MovementComponent->bUseControllerDesiredRotation = false;
		MovementComponent->RotationRate = FRotator(0.f, 540.f, 0.f);
		MovementComponent->MaxWalkSpeed = 500.f;
	}

	if (USpringArmComponent* SpringArm = GetCameraBoom())
	{
		SpringArm->TargetArmLength = 420.f;
		SpringArm->SocketOffset = FVector(0.f, 60.f, 70.f);
		SpringArm->SetUsingAbsoluteRotation(false);
		SpringArm->SetRelativeRotation(FRotator(-15.f, 0.f, 0.f));
		SpringArm->bDoCollisionTest = true;
		SpringArm->bUsePawnControlRotation = true;
		SpringArm->bInheritPitch = true;
		SpringArm->bInheritYaw = true;
		SpringArm->bInheritRoll = false;
		SpringArm->bEnableCameraLag = true;
		SpringArm->CameraLagSpeed = 12.f;
	}

	if (UCameraComponent* Camera = GetTopDownCameraComponent())
	{
		Camera->bUsePawnControlRotation = false;
		Camera->FieldOfView = 80.f;
	}
}
