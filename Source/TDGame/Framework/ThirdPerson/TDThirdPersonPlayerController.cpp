#include "Framework/ThirdPerson/TDThirdPersonPlayerController.h"

#include "Characters/TDGameCharacter.h"
#include "Combat/TDCombatLibrary.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "TDGame.h"

namespace
{
	UInputModifierNegate* CreateNegateModifier(UObject* Outer, const bool bNegateX, const bool bNegateY)
	{
		UInputModifierNegate* Modifier = NewObject<UInputModifierNegate>(Outer);
		Modifier->bX = bNegateX;
		Modifier->bY = bNegateY;
		Modifier->bZ = false;
		return Modifier;
	}

	UInputModifierSwizzleAxis* CreateSwizzleModifier(UObject* Outer)
	{
		UInputModifierSwizzleAxis* Modifier = NewObject<UInputModifierSwizzleAxis>(Outer);
		Modifier->Order = EInputAxisSwizzle::YXZ;
		return Modifier;
	}
}

ATDThirdPersonPlayerController::ATDThirdPersonPlayerController()
{
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
	DefaultMouseCursor = EMouseCursor::Default;

	DefaultMappingContext = CreateDefaultSubobject<UInputMappingContext>(TEXT("ThirdPersonMappingContext"));

	MoveAction = CreateDefaultSubobject<UInputAction>(TEXT("MoveAction"));
	MoveAction->ValueType = EInputActionValueType::Axis2D;
	MoveAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;

	LookAction = CreateDefaultSubobject<UInputAction>(TEXT("LookAction"));
	LookAction->ValueType = EInputActionValueType::Axis2D;
	LookAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;

	PrimaryAttackAction = CreateDefaultSubobject<UInputAction>(TEXT("PrimaryAttackAction"));
	PrimaryAttackAction->ValueType = EInputActionValueType::Boolean;

	RollAction = CreateDefaultSubobject<UInputAction>(TEXT("RollAction"));
	RollAction->ValueType = EInputActionValueType::Boolean;

	JumpAction = CreateDefaultSubobject<UInputAction>(TEXT("JumpAction"));
	JumpAction->ValueType = EInputActionValueType::Boolean;

	SkillQAction = CreateDefaultSubobject<UInputAction>(TEXT("SkillQAction"));
	SkillQAction->ValueType = EInputActionValueType::Boolean;

	SkillEAction = CreateDefaultSubobject<UInputAction>(TEXT("SkillEAction"));
	SkillEAction->ValueType = EInputActionValueType::Boolean;

	ConfigureDefaultInputMapping();
}

void ATDThirdPersonPlayerController::BeginPlay()
{
	Super::BeginPlay();

	SetInputMode(FInputModeGameOnly());

	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
	}
}

void ATDThirdPersonPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (InPawn)
	{
		SetControlRotation(InPawn->GetActorRotation());
	}
}

void ATDThirdPersonPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInputComponent)
	{
		UE_LOG(LogTDGame, Warning, TEXT("ATDThirdPersonPlayerController requires an EnhancedInputComponent."));
		return;
	}

	EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Move);
	EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &ThisClass::Move);
	EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Canceled, this, &ThisClass::Move);
	EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::Look);
	EnhancedInputComponent->BindAction(PrimaryAttackAction, ETriggerEvent::Started, this, &ThisClass::StartPrimaryAttack);
	EnhancedInputComponent->BindAction(RollAction, ETriggerEvent::Started, this, &ThisClass::StartRoll);
	EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ThisClass::StartJump);
	EnhancedInputComponent->BindAction(SkillQAction, ETriggerEvent::Started, this, &ThisClass::UseSkillQ);
	EnhancedInputComponent->BindAction(SkillEAction, ETriggerEvent::Started, this, &ThisClass::UseSkillE);
}

void ATDThirdPersonPlayerController::ConfigureDefaultInputMapping()
{
	if (!DefaultMappingContext)
	{
		return;
	}

	DefaultMappingContext->UnmapAll();
	AddMoveMappings();
	AddLookMappings();
	DefaultMappingContext->MapKey(PrimaryAttackAction, EKeys::LeftMouseButton);
	DefaultMappingContext->MapKey(PrimaryAttackAction, EKeys::Gamepad_RightTrigger);
	DefaultMappingContext->MapKey(RollAction, EKeys::LeftShift);
	DefaultMappingContext->MapKey(RollAction, EKeys::Gamepad_FaceButton_Right);
	DefaultMappingContext->MapKey(JumpAction, EKeys::SpaceBar);
	DefaultMappingContext->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
	DefaultMappingContext->MapKey(SkillQAction, EKeys::Q);
	DefaultMappingContext->MapKey(SkillQAction, EKeys::Gamepad_RightShoulder);
	DefaultMappingContext->MapKey(SkillEAction, EKeys::E);
	DefaultMappingContext->MapKey(SkillEAction, EKeys::Gamepad_LeftShoulder);
}

void ATDThirdPersonPlayerController::AddMoveMappings()
{
	FEnhancedActionKeyMapping& MoveForward = DefaultMappingContext->MapKey(MoveAction, EKeys::W);
	MoveForward.Modifiers.Add(CreateSwizzleModifier(DefaultMappingContext));

	FEnhancedActionKeyMapping& MoveBackward = DefaultMappingContext->MapKey(MoveAction, EKeys::S);
	MoveBackward.Modifiers.Add(CreateSwizzleModifier(DefaultMappingContext));
	MoveBackward.Modifiers.Add(CreateNegateModifier(DefaultMappingContext, false, true));

	FEnhancedActionKeyMapping& MoveRight = DefaultMappingContext->MapKey(MoveAction, EKeys::D);
	MoveRight.Modifiers.Reset();

	FEnhancedActionKeyMapping& MoveLeft = DefaultMappingContext->MapKey(MoveAction, EKeys::A);
	MoveLeft.Modifiers.Add(CreateNegateModifier(DefaultMappingContext, true, false));

	FEnhancedActionKeyMapping& GamepadMoveX = DefaultMappingContext->MapKey(MoveAction, EKeys::Gamepad_LeftX);
	GamepadMoveX.Modifiers.Reset();

	FEnhancedActionKeyMapping& GamepadMoveY = DefaultMappingContext->MapKey(MoveAction, EKeys::Gamepad_LeftY);
	GamepadMoveY.Modifiers.Add(CreateSwizzleModifier(DefaultMappingContext));
}

void ATDThirdPersonPlayerController::AddLookMappings()
{
	FEnhancedActionKeyMapping& MouseLookX = DefaultMappingContext->MapKey(LookAction, EKeys::MouseX);
	MouseLookX.Modifiers.Reset();

	FEnhancedActionKeyMapping& MouseLookY = DefaultMappingContext->MapKey(LookAction, EKeys::MouseY);
	MouseLookY.Modifiers.Add(CreateSwizzleModifier(DefaultMappingContext));
	MouseLookY.Modifiers.Add(CreateNegateModifier(DefaultMappingContext, false, true));

	FEnhancedActionKeyMapping& GamepadLookX = DefaultMappingContext->MapKey(LookAction, EKeys::Gamepad_RightX);
	GamepadLookX.Modifiers.Reset();

	FEnhancedActionKeyMapping& GamepadLookY = DefaultMappingContext->MapKey(LookAction, EKeys::Gamepad_RightY);
	GamepadLookY.Modifiers.Add(CreateSwizzleModifier(DefaultMappingContext));
	GamepadLookY.Modifiers.Add(CreateNegateModifier(DefaultMappingContext, false, true));
}

void ATDThirdPersonPlayerController::Move(const FInputActionValue& Value)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		CachedMovementInput = FVector2D::ZeroVector;
		return;
	}

	CachedMovementInput = Value.Get<FVector2D>();
	if (CachedMovementInput.IsNearlyZero())
	{
		return;
	}

	const FVector MovementDirection = GetMovementDirection(CachedMovementInput);
	if (MovementDirection.IsNearlyZero())
	{
		return;
	}

	const FRotator YawRotation(0.f, GetControlRotation().Yaw, 0.f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	ControlledPawn->AddMovementInput(ForwardDirection, CachedMovementInput.Y);
	ControlledPawn->AddMovementInput(RightDirection, CachedMovementInput.X);
}

void ATDThirdPersonPlayerController::Look(const FInputActionValue& Value)
{
	const FVector2D LookInput = Value.Get<FVector2D>();
	if (LookInput.IsNearlyZero())
	{
		return;
	}

	AddYawInput(LookInput.X);
	AddPitchInput(LookInput.Y);
}

void ATDThirdPersonPlayerController::StartPrimaryAttack()
{
	ATDGameCharacter* PlayerCharacter = GetPlayerCharacter();
	if (!PlayerCharacter)
	{
		return;
	}

	AActor* TargetActor = ResolveHostileTargetFromView(PlayerCharacter);
	if (!TargetActor)
	{
		return;
	}

	const FVector ToTarget = TargetActor->GetActorLocation() - PlayerCharacter->GetActorLocation();
	if (!ToTarget.IsNearlyZero())
	{
		PlayerCharacter->SetActorRotation(ToTarget.Rotation());
	}

	PlayerCharacter->ExecutePrimaryAttack(TargetActor);
}

void ATDThirdPersonPlayerController::UseSkillQ()
{
	if (ATDGameCharacter* PlayerCharacter = GetPlayerCharacter())
	{
		PlayerCharacter->ExecuteSkillQ(ResolveHostileTargetFromView(PlayerCharacter));
	}
}

void ATDThirdPersonPlayerController::UseSkillE()
{
	if (ATDGameCharacter* PlayerCharacter = GetPlayerCharacter())
	{
		PlayerCharacter->ExecuteSkillE(ResolveHostileTargetFromView(PlayerCharacter));
	}
}

void ATDThirdPersonPlayerController::StartRoll()
{
	if (ATDGameCharacter* PlayerCharacter = GetPlayerCharacter())
	{
		PlayerCharacter->TryStartRoll(GetRollDirection(*PlayerCharacter));
	}
}

void ATDThirdPersonPlayerController::StartJump()
{
	if (ATDGameCharacter* PlayerCharacter = GetPlayerCharacter())
	{
		PlayerCharacter->TryStartJumpAbility();
	}
}

ATDGameCharacter* ATDThirdPersonPlayerController::GetPlayerCharacter() const
{
	return Cast<ATDGameCharacter>(GetPawn());
}

AActor* ATDThirdPersonPlayerController::ResolveHostileTargetFromView(ATDGameCharacter* PlayerCharacter) const
{
	if (!PlayerCharacter || !GetWorld())
	{
		return nullptr;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector TraceEnd = ViewLocation + (ViewRotation.Vector() * TargetingTraceDistance);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDThirdPersonTargetSweep), false, PlayerCharacter);
	QueryParams.AddIgnoredActor(PlayerCharacter);

	TArray<FHitResult> HitResults;
	const bool bHitAnything = GetWorld()->SweepMultiByChannel(
		HitResults,
		ViewLocation,
		TraceEnd,
		FQuat::Identity,
		TargetTraceChannel,
		FCollisionShape::MakeSphere(TargetingSweepRadius),
		QueryParams);

	if (!bHitAnything)
	{
		return nullptr;
	}

	AActor* BestTarget = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();

	for (const FHitResult& HitResult : HitResults)
	{
		AActor* HitActor = HitResult.GetActor();
		if (!HitActor ||
			!UTDCombatLibrary::IsActorAlive(HitActor) ||
			!UTDCombatLibrary::AreActorsHostile(PlayerCharacter, HitActor))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(ViewLocation, HitActor->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestTarget = HitActor;
		}
	}

	return BestTarget;
}

FVector ATDThirdPersonPlayerController::GetMovementDirection(const FVector2D& MovementInput) const
{
	if (MovementInput.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	const FRotator YawRotation(0.f, GetControlRotation().Yaw, 0.f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	FVector MovementDirection = (ForwardDirection * MovementInput.Y) + (RightDirection * MovementInput.X);
	MovementDirection.Z = 0.f;
	return MovementDirection.GetSafeNormal();
}

FVector ATDThirdPersonPlayerController::GetRollDirection(const APawn& ControlledPawn) const
{
	if (!CachedMovementInput.IsNearlyZero())
	{
		const FVector MovementDirection = GetMovementDirection(CachedMovementInput);
		if (!MovementDirection.IsNearlyZero())
		{
			return MovementDirection;
		}
	}

	FVector FacingDirection = ControlledPawn.GetActorForwardVector();
	FacingDirection.Z = 0.f;
	return FacingDirection.IsNearlyZero() ? FVector::ForwardVector : FacingDirection.GetSafeNormal();
}
