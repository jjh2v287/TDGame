// Copyright Epic Games, Inc. All Rights Reserved.

#include "TDGamePlayerController.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDDamageTarget.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "InputCoreTypes.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "TDGameCharacter.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "InputActionValue.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "TDGame.h"

ATDGamePlayerController::ATDGamePlayerController()
{
	bIsTouch = false;
	bMoveToMouseCursor = false;

	// create the path following comp
	PathFollowingComponent = CreateDefaultSubobject<UPathFollowingComponent>(TEXT("Path Following Component"));

	// configure the controller
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
	CachedDestination = FVector::ZeroVector;
	FollowTime = 0.f;
}

void ATDGamePlayerController::SetupInputComponent()
{
	// set up gameplay key bindings
	Super::SetupInputComponent();

	// Only set up input on local player controllers
	if (IsLocalPlayerController())
	{
		InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ATDGamePlayerController::CastFirstDamageSpell);
		InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ATDGamePlayerController::CastSecondDamageSpell);
		InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ATDGamePlayerController::CastThirdDamageSpell);
		InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ATDGamePlayerController::CastFourthDamageSpell);
		InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &ATDGamePlayerController::CastFifthDamageSpell);
		InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &ATDGamePlayerController::CastSixthDamageSpell);

		// Add Input Mapping Context
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}

		// Set up action bindings
		if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent))
		{
			SetupKeyboardMovement();
			EnhancedInputComponent->BindAction(KeyboardMoveAction, ETriggerEvent::Triggered, this, &ATDGamePlayerController::OnKeyboardMovementTriggered);
			EnhancedInputComponent->BindAction(KeyboardMoveAction, ETriggerEvent::Completed, this, &ATDGamePlayerController::OnKeyboardMovementStopped);
			EnhancedInputComponent->BindAction(KeyboardMoveAction, ETriggerEvent::Canceled, this, &ATDGamePlayerController::OnKeyboardMovementStopped);

			// Setup mouse input events
			EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Started, this, &ATDGamePlayerController::OnInputStarted);
			EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Triggered, this, &ATDGamePlayerController::OnSetDestinationTriggered);
			EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Completed, this, &ATDGamePlayerController::OnSetDestinationReleased);
			EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Canceled, this, &ATDGamePlayerController::OnPointerMovementCanceled);

			// Setup touch input events
			EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Started, this, &ATDGamePlayerController::OnTouchStarted);
			EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Triggered, this, &ATDGamePlayerController::OnTouchTriggered);
			EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Completed, this, &ATDGamePlayerController::OnTouchReleased);
			EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Canceled, this, &ATDGamePlayerController::OnPointerMovementCanceled);
		}
		else
		{
			UE_LOG(LogTDGame, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
		}
	}
}

void ATDGamePlayerController::OnInputStarted()
{
	StartPointerMovement(false);
}

void ATDGamePlayerController::OnSetDestinationTriggered()
{
	// Update the move destination to wherever the cursor is pointing at
	UpdateCachedDestination();
}

void ATDGamePlayerController::OnSetDestinationReleased()
{
	if (!bIsPointerInputActive)
	{
		return;
	}
	bIsPointerInputActive = false;
	bHasPendingPointerRelease = true;
}

void ATDGamePlayerController::OnTouchStarted()
{
	StartPointerMovement(true);
}

// Triggered every frame when the input is held down
void ATDGamePlayerController::OnTouchTriggered()
{
	bIsTouch = true;
	OnSetDestinationTriggered();
}

void ATDGamePlayerController::OnTouchReleased()
{
	OnSetDestinationReleased();
	bIsTouch = false;
}

void ATDGamePlayerController::UpdateCachedDestination()
{
	// We look for the location in the world where the player has pressed the input
	FHitResult Hit;
	bool bHitSuccessful = false;
	if (bIsTouch)
	{
		bHitSuccessful = GetHitResultUnderFinger(ETouchIndex::Touch1, ECollisionChannel::ECC_Visibility, true, Hit);
	}
	else
	{
		bHitSuccessful = GetCursorHit(Hit);
	}
	bHasValidPointerDestination = bHitSuccessful;

	// If we hit a surface, cache the location
	if (bHitSuccessful)
	{
		CachedDestination = Hit.Location;
	}
}

void ATDGamePlayerController::SetupKeyboardMovement()
{
	if (!KeyboardMoveAction)
	{
		KeyboardMoveAction = NewObject<UInputAction>(this, TEXT("TDKeyboardMove"));
		KeyboardMoveAction->ValueType = EInputActionValueType::Axis2D;
		KeyboardMoveAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
		KeyboardMappingContext = NewObject<UInputMappingContext>(this, TEXT("TDKeyboardMovement"));
		UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(KeyboardMappingContext);
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(KeyboardMappingContext);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		KeyboardMappingContext->MapKey(KeyboardMoveAction, EKeys::D);
		KeyboardMappingContext->MapKey(KeyboardMoveAction, EKeys::A).Modifiers.Add(Negate);
		KeyboardMappingContext->MapKey(KeyboardMoveAction, EKeys::W).Modifiers.Add(Swizzle);
		FEnhancedActionKeyMapping& BackwardMapping = KeyboardMappingContext->MapKey(KeyboardMoveAction, EKeys::S);
		BackwardMapping.Modifiers.Add(Negate);
		BackwardMapping.Modifiers.Add(Swizzle);
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(KeyboardMappingContext, 1);
	}
}

void ATDGamePlayerController::OnKeyboardMovementTriggered(const FInputActionValue& Value)
{
	KeyboardMoveInput = Value.Get<FVector2D>().GetClampedToMaxSize(1.f);
}

void ATDGamePlayerController::OnKeyboardMovementStopped()
{
	KeyboardMoveInput = FVector2D::ZeroVector;
}

void ATDGamePlayerController::StartPointerMovement(bool bTouchInput)
{
	bIsTouch = bTouchInput;
	bIsPointerInputActive = true;
	bHasPointerStarted = true;
	bHasPendingPointerRelease = false;
	bShouldIgnorePointerUntilRelease = false;
	FollowTime = 0.f;
	UpdateCachedDestination();
}

void ATDGamePlayerController::OnPointerMovementCanceled()
{
	bIsPointerInputActive = false;
	bHasPointerStarted = false;
	bHasPendingPointerRelease = false;
	bShouldIgnorePointerUntilRelease = false;
	bHasValidPointerDestination = false;
	bIsTouch = false;
	FollowTime = 0.f;
	if (!bIsUsingKeyboardMovement)
	{
		StopMovement();
	}
}

void ATDGamePlayerController::SetKeyboardMovementMode(bool bKeyboardMode)
{
	if (bIsUsingKeyboardMovement != bKeyboardMode)
	{
		StopMovement();
	}
	bIsUsingKeyboardMovement = bKeyboardMode;
	if (ATDGameCharacter* ControlledCharacter = Cast<ATDGameCharacter>(GetPawn()))
	{
		ControlledCharacter->bUseControllerRotationYaw = false;
		ControlledCharacter->GetCharacterMovement()->bUseControllerDesiredRotation = false;
		ControlledCharacter->GetCharacterMovement()->bOrientRotationToMovement = !bKeyboardMode;
	}
}

void ATDGamePlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PostProcessInput(DeltaTime, bGamePaused);
	ATDGameCharacter* ControlledCharacter = Cast<ATDGameCharacter>(GetPawn());
	if (!IsLocalPlayerController() || !ControlledCharacter)
	{
		return;
	}
	const UTDCombatComponent* Combat = ControlledCharacter->GetCombatComponent();
	if (bGamePaused || IsMoveInputIgnored() || !Combat || !Combat->IsAlive() || Combat->IsFrozen())
	{
		StopMovement();
		KeyboardMoveInput = FVector2D::ZeroVector;
		bShouldIgnorePointerUntilRelease = true;
		bHasPointerStarted = false;
		bHasPendingPointerRelease = false;
		FollowTime = 0.f;
		return;
	}
	if (!KeyboardMoveInput.IsNearlyZero())
	{
		SetKeyboardMovementMode(true);
		if (bIsPointerInputActive || bHasPointerStarted || bHasPendingPointerRelease)
		{
			bShouldIgnorePointerUntilRelease = true;
		}
		bHasPointerStarted = false;
		bHasPendingPointerRelease = false;
		FollowTime = 0.f;
		const float CameraYaw = ControlledCharacter->GetTopDownCameraComponent()->GetComponentRotation().Yaw;
		const FRotationMatrix CameraAxes(FRotator(0.f, CameraYaw, 0.f));
		const FVector MoveDirection = CameraAxes.GetUnitAxis(EAxis::X) * KeyboardMoveInput.Y
			+ CameraAxes.GetUnitAxis(EAxis::Y) * KeyboardMoveInput.X;
		ControlledCharacter->AddMovementInput(MoveDirection, 1.f);
		FaceMouseCursor(ControlledCharacter);
		return;
	}
	if (bHasPointerStarted && !bShouldIgnorePointerUntilRelease)
	{
		SetKeyboardMovementMode(false);
		StopMovement();
	}
	bHasPointerStarted = false;
	if (bIsPointerInputActive && !bShouldIgnorePointerUntilRelease)
	{
		FollowTime += DeltaTime;
		if (bHasValidPointerDestination)
		{
			const FVector MoveDirection = (CachedDestination - ControlledCharacter->GetActorLocation()).GetSafeNormal2D();
			ControlledCharacter->AddMovementInput(MoveDirection, 1.f);
		}
	}
	if (bHasPendingPointerRelease)
	{
		if (!bShouldIgnorePointerUntilRelease && bHasValidPointerDestination && FollowTime <= ShortPressThreshold)
		{
			UAIBlueprintHelperLibrary::SimpleMoveToLocation(this, CachedDestination);
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, FXCursor, CachedDestination, FRotator::ZeroRotator, FVector::OneVector, true, true, ENCPoolMethod::None, true);
		}
		bHasPendingPointerRelease = false;
		bShouldIgnorePointerUntilRelease = false;
		bHasValidPointerDestination = false;
		FollowTime = 0.f;
	}
	if (bIsUsingKeyboardMovement)
	{
		FaceMouseCursor(ControlledCharacter);
	}
}

bool ATDGamePlayerController::GetCursorHit(FHitResult& Hit) const
{
	float CursorX = 0.f;
	float CursorY = 0.f;
	int32 Width = 0;
	int32 Height = 0;
	GetViewportSize(Width, Height);
	if (!GetMousePosition(CursorX, CursorY) || CursorX < 0.f || CursorY < 0.f || CursorX >= Width || CursorY >= Height)
	{
		return false;
	}
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDPlayerCursor), true, GetPawn());
	return GetHitResultAtScreenPosition(FVector2D(CursorX, CursorY), ECC_Visibility, QueryParams, Hit);
}

void ATDGamePlayerController::FaceMouseCursor(ATDGameCharacter* ControlledCharacter) const
{
	float CursorX = 0.f;
	float CursorY = 0.f;
	int32 Width = 0;
	int32 Height = 0;
	GetViewportSize(Width, Height);
	if (!GetMousePosition(CursorX, CursorY) || CursorX < 0.f || CursorY < 0.f || CursorX >= Width || CursorY >= Height)
	{
		return;
	}
	FHitResult Hit;
	FVector AimLocation;
	if (GetCursorHit(Hit))
	{
		AimLocation = Hit.ImpactPoint;
	}
	else
	{
		FVector RayOrigin;
		FVector RayDirection;
		if (!DeprojectScreenPositionToWorld(CursorX, CursorY, RayOrigin, RayDirection) || FMath::IsNearlyZero(RayDirection.Z))
		{
			return;
		}
		const double Distance = (ControlledCharacter->GetActorLocation().Z - RayOrigin.Z) / RayDirection.Z;
		if (!FMath::IsFinite(Distance) || Distance < 0.)
		{
			return;
		}
		AimLocation = RayOrigin + RayDirection * Distance;
	}
	const FVector FacingDirection = (AimLocation - ControlledCharacter->GetActorLocation()).GetSafeNormal2D();
	if (!FacingDirection.IsNearlyZero() && !FacingDirection.ContainsNaN())
	{
		ControlledCharacter->SetActorRotation(FRotator(0.f, FacingDirection.Rotation().Yaw, 0.f));
	}
}

void ATDGamePlayerController::ResetMovementInput()
{
	KeyboardMoveInput = FVector2D::ZeroVector;
	OnPointerMovementCanceled();
	SetKeyboardMovementMode(false);
	StopMovement();
}

void ATDGamePlayerController::FlushPressedKeys()
{
	ResetMovementInput();
	Super::FlushPressedKeys();
}

void ATDGamePlayerController::OnUnPossess()
{
	ResetMovementInput();
	Super::OnUnPossess();
}

void ATDGamePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetMovementInput();
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (KeyboardMappingContext)
		{
			Subsystem->RemoveMappingContext(KeyboardMappingContext);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ATDGamePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalPlayerController() && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::White, TEXT("WASD Move + Mouse Aim | Left Click Move\n1 Fireball | 2 Blizzard | 3 Mine | 4 Shockwave | 5 Meteor | 6 Delayed Homing\nConsole: TDSpawnDamageTargets / TDSetCasterLevel 10"));
	}
}

void ATDGamePlayerController::CastDamageSpell(int32 Slot)
{
	ATDGameCharacter* ControlledCharacter = Cast<ATDGameCharacter>(GetPawn());
	if (!ControlledCharacter)
	{
		return;
	}

	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, true, Hit))
	{
		return;
	}
	ControlledCharacter->CastDamageSpell(Slot, Hit.ImpactPoint);
}

void ATDGamePlayerController::CastFirstDamageSpell()
{
	CastDamageSpell(0);
}

void ATDGamePlayerController::CastSecondDamageSpell()
{
	CastDamageSpell(1);
}

void ATDGamePlayerController::CastThirdDamageSpell()
{
	CastDamageSpell(2);
}

void ATDGamePlayerController::CastFourthDamageSpell()
{
	CastDamageSpell(3);
}

void ATDGamePlayerController::CastFifthDamageSpell()
{
	CastDamageSpell(4);
}

void ATDGamePlayerController::CastSixthDamageSpell()
{
	CastDamageSpell(5);
}

void ATDGamePlayerController::TDSpawnDamageTargets()
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !GetWorld())
	{
		return;
	}

	FHitResult CursorHit;
	FVector Center = ControlledPawn->GetActorLocation() + ControlledPawn->GetActorForwardVector() * 450.f;
	if (GetHitResultUnderCursor(ECC_Visibility, true, CursorHit))
	{
		Center = CursorHit.ImpactPoint;
	}

	const FVector Offsets[] = { FVector::ZeroVector, FVector(170.f, 0.f, 0.f), FVector(-170.f, 0.f, 0.f), FVector(0.f, 170.f, 0.f), FVector(0.f, -170.f, 0.f) };
	for (const FVector& Offset : Offsets)
	{
		FVector Location = Center + Offset;
		FHitResult GroundHit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDSpawnDamageTargets), false, ControlledPawn);
		if (GetWorld()->LineTraceSingleByChannel(GroundHit, Location + FVector(0.f, 0.f, 500.f), Location - FVector(0.f, 0.f, 2000.f), ECC_Visibility, QueryParams))
		{
			Location = GroundHit.ImpactPoint;
		}
		else
		{
			Location.Z = ControlledPawn->GetActorLocation().Z;
			if (const ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn))
			{
				Location.Z -= ControlledCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			}
		}
		Location.Z += 65.f;
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
		GetWorld()->SpawnActor<ATDDamageTarget>(Location, FRotator::ZeroRotator, Parameters);
	}
}

void ATDGamePlayerController::TDSetCasterLevel(int32 Level)
{
	ATDGameCharacter* ControlledCharacter = Cast<ATDGameCharacter>(GetPawn());
	if (!ControlledCharacter || !ControlledCharacter->GetCombatComponent())
	{
		return;
	}

	UTDCombatComponent* Combat = ControlledCharacter->GetCombatComponent();
	Combat->SetCombatLevel(Level);
	const FTDCombatStats Stats = Combat->GetStats();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, FString::Printf(TEXT("Caster level %d | Attack %.1f | Spell %.1f"), Stats.Level, Stats.GetAttackPower(), Stats.GetSpellPower()));
	}
}
