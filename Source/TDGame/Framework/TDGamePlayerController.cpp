// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/TDGamePlayerController.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDCombatLibrary.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "Combat/Damage/TDDamageTarget.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "InputCoreTypes.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "Characters/TDGameCharacter.h"
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
#include "Engine/GameInstance.h"
#include "World/Streaming/TDSeamlessTravelSubsystem.h"
#include "World/Persistence/TDWorldStateSubsystem.h"

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

			SetupCombatInputBindings(EnhancedInputComponent);
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
	if (LastAttackCommandFrame == GFrameCounter || (bAttackHostileOnClick && TryIssueAttackUnderCursor()))
	{
		SuppressPointerMovementUntilRelease();
	}
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
		ClearQueuedAttack();
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
		ClearQueuedAttack();
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
			ClearQueuedAttack();
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
	UpdateQueuedAttack();
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
	if (ControlledCharacter->IsRollPlaying())
	{
		return;
	}
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
	ClearQueuedAttack();
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

void ATDGamePlayerController::SetupCombatInputBindings(UEnhancedInputComponent* EnhancedInputComponent)
{
	if (PrimaryAttackAction)
	{
		EnhancedInputComponent->BindAction(PrimaryAttackAction, ETriggerEvent::Started, this, &ATDGamePlayerController::OnPrimaryAttackStarted);
	}
	if (RollAction)
	{
		EnhancedInputComponent->BindAction(RollAction, ETriggerEvent::Started, this, &ATDGamePlayerController::OnRollStarted);
	}
	if (JumpAction)
	{
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ATDGamePlayerController::OnJumpStarted);
	}
	if (SkillQAction)
	{
		EnhancedInputComponent->BindAction(SkillQAction, ETriggerEvent::Started, this, &ATDGamePlayerController::OnSkillQStarted);
	}
	if (SkillEAction)
	{
		EnhancedInputComponent->BindAction(SkillEAction, ETriggerEvent::Started, this, &ATDGamePlayerController::OnSkillEStarted);
	}
}

void ATDGamePlayerController::OnPrimaryAttackStarted()
{
	if (TryIssueAttackUnderCursor())
	{
		SuppressPointerMovementUntilRelease();
	}
}

void ATDGamePlayerController::OnSkillQStarted()
{
	ATDGameCharacter* PlayerCharacter = Cast<ATDGameCharacter>(GetPawn());
	if (!PlayerCharacter)
	{
		return;
	}

	if (PlayerCharacter->ExecuteSkillQ(ResolveHostileTargetUnderCursor(PlayerCharacter)))
	{
		ClearQueuedAttack();
		StopMovement();
	}
}

void ATDGamePlayerController::OnSkillEStarted()
{
	ATDGameCharacter* PlayerCharacter = Cast<ATDGameCharacter>(GetPawn());
	if (!PlayerCharacter)
	{
		return;
	}

	if (PlayerCharacter->ExecuteSkillE(ResolveHostileTargetUnderCursor(PlayerCharacter)))
	{
		ClearQueuedAttack();
		StopMovement();
	}
}

void ATDGamePlayerController::OnRollStarted()
{
	ATDGameCharacter* PlayerCharacter = Cast<ATDGameCharacter>(GetPawn());
	if (!PlayerCharacter)
	{
		return;
	}

	if (PlayerCharacter->TryStartRoll(GetRollDirection(*PlayerCharacter)))
	{
		ClearQueuedAttack();
		StopMovement();
	}
}

void ATDGamePlayerController::OnJumpStarted()
{
	if (ATDGameCharacter* PlayerCharacter = Cast<ATDGameCharacter>(GetPawn()))
	{
		PlayerCharacter->TryStartJumpAbility();
	}
}

bool ATDGamePlayerController::TryIssueAttackUnderCursor()
{
	ATDGameCharacter* PlayerCharacter = Cast<ATDGameCharacter>(GetPawn());
	if (!PlayerCharacter)
	{
		return false;
	}

	AActor* TargetActor = ResolveHostileTargetUnderCursor(PlayerCharacter);
	if (!TargetActor)
	{
		return false;
	}

	IssueAttackCommand(TargetActor);
	LastAttackCommandFrame = GFrameCounter;
	return true;
}

void ATDGamePlayerController::SuppressPointerMovementUntilRelease()
{
	bShouldIgnorePointerUntilRelease = true;
	bHasPointerStarted = false;
	bHasPendingPointerRelease = false;
	FollowTime = 0.f;
}

AActor* ATDGamePlayerController::ResolveHostileTargetUnderCursor(const ATDGameCharacter* PlayerCharacter) const
{
	if (!PlayerCharacter)
	{
		return nullptr;
	}

	FHitResult Hit;
	if (!GetCursorHit(Hit))
	{
		return nullptr;
	}

	AActor* HitActor = Hit.GetActor();
	if (!HitActor || !UTDCombatLibrary::IsActorAlive(HitActor) || !UTDCombatLibrary::AreActorsHostile(PlayerCharacter, HitActor))
	{
		return nullptr;
	}

	return HitActor;
}

void ATDGamePlayerController::IssueAttackCommand(AActor* TargetActor)
{
	ATDGameCharacter* PlayerCharacter = Cast<ATDGameCharacter>(GetPawn());
	if (!PlayerCharacter || !TargetActor)
	{
		return;
	}

	const float DistanceToTarget = FVector::Dist2D(PlayerCharacter->GetActorLocation(), TargetActor->GetActorLocation());
	const float DesiredRange = PlayerCharacter->GetDesiredAttackRange() + PlayerCharacter->GetAttackAcceptanceRadius();
	if (DistanceToTarget > DesiredRange)
	{
		QueuedAttackTarget = TargetActor;
		bIsAttackMoveQueued = true;
		SetKeyboardMovementMode(false);
		UAIBlueprintHelperLibrary::SimpleMoveToActor(this, TargetActor);
		return;
	}

	ClearQueuedAttack();
	StopMovement();
	PlayerCharacter->ExecutePrimaryAttack(TargetActor);
}

void ATDGamePlayerController::UpdateQueuedAttack()
{
	if (!bIsAttackMoveQueued)
	{
		return;
	}

	ATDGameCharacter* PlayerCharacter = Cast<ATDGameCharacter>(GetPawn());
	AActor* TargetActor = QueuedAttackTarget.Get();
	if (!PlayerCharacter || !TargetActor)
	{
		ClearQueuedAttack();
		return;
	}

	if (!UTDCombatLibrary::IsActorAlive(TargetActor) || !UTDCombatLibrary::AreActorsHostile(PlayerCharacter, TargetActor))
	{
		ClearQueuedAttack();
		return;
	}

	const float AttackDistance = FVector::Dist2D(PlayerCharacter->GetActorLocation(), TargetActor->GetActorLocation());
	const float DesiredRange = PlayerCharacter->GetDesiredAttackRange() + PlayerCharacter->GetAttackAcceptanceRadius();
	if (AttackDistance > DesiredRange)
	{
		return;
	}

	StopMovement();
	if (PlayerCharacter->ExecutePrimaryAttack(TargetActor))
	{
		ClearQueuedAttack();
	}
}

void ATDGamePlayerController::ClearQueuedAttack()
{
	QueuedAttackTarget.Reset();
	bIsAttackMoveQueued = false;
}

FVector ATDGamePlayerController::GetRollDirection(const ATDGameCharacter& ControlledCharacter) const
{
	const FVector2D SnappedMovementInput = SnapMovementInputToEightDirections(KeyboardMoveInput);
	if (SnappedMovementInput.IsNearlyZero())
	{
		FVector FacingDirection = ControlledCharacter.GetActorForwardVector();
		FacingDirection.Z = 0.f;
		return FacingDirection.IsNearlyZero() ? FVector::ForwardVector : FacingDirection.GetSafeNormal();
	}

	const float CameraYaw = ControlledCharacter.GetTopDownCameraComponent()->GetComponentRotation().Yaw;
	const FRotationMatrix CameraAxes(FRotator(0.f, CameraYaw, 0.f));
	FVector RollDirection = CameraAxes.GetUnitAxis(EAxis::X) * SnappedMovementInput.Y + CameraAxes.GetUnitAxis(EAxis::Y) * SnappedMovementInput.X;
	RollDirection.Z = 0.f;
	return RollDirection.IsNearlyZero() ? FVector::ForwardVector : RollDirection.GetSafeNormal();
}

FVector2D ATDGamePlayerController::SnapMovementInputToEightDirections(const FVector2D& MovementInput)
{
	if (MovementInput.IsNearlyZero())
	{
		return FVector2D::ZeroVector;
	}

	const float AngleRadians = FMath::Atan2(MovementInput.X, MovementInput.Y);
	const float AngleDegrees = FMath::RadiansToDegrees(AngleRadians);
	const float SnappedAngleDegrees = FMath::RoundToFloat(AngleDegrees / 45.f) * 45.f;
	const float SnappedAngleRadians = FMath::DegreesToRadians(SnappedAngleDegrees);
	return FVector2D(FMath::Sin(SnappedAngleRadians), FMath::Cos(SnappedAngleRadians));
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

void ATDGamePlayerController::TDPlayMeleeMontage(const FString& MontagePath)
{
	ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn());
	if (!ControlledCharacter || !ControlledCharacter->GetMesh())
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDPlayMeleeMontage: no controlled character with a skeletal mesh."));
		return;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
	if (!Montage)
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDPlayMeleeMontage: failed to load montage '%s'."), *MontagePath);
		return;
	}

	const float Duration = ControlledCharacter->PlayAnimMontage(Montage);
	UE_LOG(LogTDGame, Log, TEXT("TDPlayMeleeMontage: '%s' on '%s' (duration %.2f)."), *Montage->GetName(), *ControlledCharacter->GetName(), Duration);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, Duration > 0.f ? FColor::Green : FColor::Red, FString::Printf(TEXT("PlayMeleeMontage %s -> %.2fs"), *Montage->GetName(), Duration));
	}
}

void ATDGamePlayerController::TDTravelToDungeon(FName DungeonId)
{
	UTDSeamlessTravelSubsystem* Travel = GetWorld() ? GetWorld()->GetSubsystem<UTDSeamlessTravelSubsystem>() : nullptr;
	if (!Travel)
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDTravelToDungeon: no seamless travel subsystem in this world."));
		return;
	}

	const bool bAccepted = Travel->RequestTravel(DungeonId, true);
	UE_LOG(LogTDGame, Log, TEXT("TDTravelToDungeon '%s': %s."), *DungeonId.ToString(), bAccepted ? TEXT("requested") : TEXT("refused"));
}

void ATDGamePlayerController::TDTravelToField()
{
	UTDSeamlessTravelSubsystem* Travel = GetWorld() ? GetWorld()->GetSubsystem<UTDSeamlessTravelSubsystem>() : nullptr;
	if (!Travel)
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDTravelToField: no seamless travel subsystem in this world."));
		return;
	}

	const FName DungeonId = Travel->GetLastTraveledDungeonId();
	if (DungeonId.IsNone())
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDTravelToField: no dungeon was traveled to yet; use TDTravelToDungeon first."));
		return;
	}

	const bool bAccepted = Travel->RequestTravel(DungeonId, false);
	UE_LOG(LogTDGame, Log, TEXT("TDTravelToField from '%s': %s."), *DungeonId.ToString(), bAccepted ? TEXT("requested") : TEXT("refused"));
}

void ATDGamePlayerController::TDSaveWorldState(const FString& SlotName)
{
	UTDWorldStateSubsystem* WorldState = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTDWorldStateSubsystem>() : nullptr;
	if (!WorldState)
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDSaveWorldState: no world state subsystem."));
		return;
	}

	WorldState->SaveToSlot(SlotName);
}

void ATDGamePlayerController::TDLoadWorldState(const FString& SlotName)
{
	UTDWorldStateSubsystem* WorldState = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTDWorldStateSubsystem>() : nullptr;
	if (!WorldState)
	{
		UE_LOG(LogTDGame, Warning, TEXT("TDLoadWorldState: no world state subsystem."));
		return;
	}

	WorldState->LoadFromSlot(SlotName);
}
