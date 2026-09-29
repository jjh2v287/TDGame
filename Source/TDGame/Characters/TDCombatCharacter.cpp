#include "Characters/TDCombatCharacter.h"
#include "Abilities/GameplayAbility.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Backends/MoverStandaloneLiaison.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/GAS/TDCombatAttributeSet.h"
#include "Combat/TDCombatComponent.h"
#include "Component/AnimNextComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/InstantMovementEffects/BasicInstantMovementEffects.h"
#include "DefaultMovementSet/NavMoverComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Controller.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "Module/UAFWeakSystemReference.h"
#include "MovementMode.h"

const FName ATDCombatCharacter::CapsuleComponentName(TEXT("CollisionCylinder"));
const FName ATDCombatCharacter::MeshComponentName(TEXT("CharacterMesh0"));

namespace TDCombatCharacterMovement
{
	constexpr float StandardCapsuleRadius = 34.f;
	constexpr float StandardCapsuleHalfHeight = 88.f;
	constexpr float MinimumOrientationInputSize = 1e-3f;

	const FName& GetAnimationPrePhysicsEventName()
	{
		static const FName EventName(TEXT("PrePhysics"));
		return EventName;
	}

	bool IsMoverReady(const UMoverComponent* MoverComponent)
	{
		return MoverComponent && MoverComponent->HasBegunPlay();
	}

	bool IsRestorableMovementMode(const FName MovementModeName)
	{
		return !MovementModeName.IsNone() && MovementModeName != UNullMovementMode::NullModeName;
	}

	FVector MakePlanarDirection(const FRotator& Rotation)
	{
		return FRotator(0.f, Rotation.Yaw, 0.f).Vector();
	}

	void GrantStartupAbilitiesAndEffects(UTDCombatComponent& CombatComponent, const TArray<TSubclassOf<UGameplayAbility>>& Abilities, const TArray<TSubclassOf<UGameplayEffect>>& Effects, UObject* SourceObject)
	{
		const int32 AbilityLevel = CombatComponent.GetStats().Level;
		for (const TSubclassOf<UGameplayAbility>& AbilityClass : Abilities)
		{
			if (AbilityClass)
			{
				CombatComponent.GiveAbility(FGameplayAbilitySpec(AbilityClass, AbilityLevel, INDEX_NONE, SourceObject));
			}
		}

		FGameplayEffectContextHandle EffectContext = CombatComponent.MakeEffectContext();
		EffectContext.AddSourceObject(SourceObject);
		for (const TSubclassOf<UGameplayEffect>& EffectClass : Effects)
		{
			if (EffectClass)
			{
				CombatComponent.ApplyGameplayEffectToSelf(EffectClass.GetDefaultObject(), AbilityLevel, EffectContext);
			}
		}
	}
}

ATDCombatCharacter::ATDCombatCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetReplicatingMovement(false);

	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(CapsuleComponentName);
	CapsuleComponent->InitCapsuleSize(TDCombatCharacterMovement::StandardCapsuleRadius, TDCombatCharacterMovement::StandardCapsuleHalfHeight);
	CapsuleComponent->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	CapsuleComponent->CanCharacterStepUpOn = ECB_No;
	CapsuleComponent->SetShouldUpdatePhysicsVolume(true);
	CapsuleComponent->SetCanEverAffectNavigation(false);
	CapsuleComponent->bDynamicObstacle = true;
	RootComponent = CapsuleComponent;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(MeshComponentName);
	Mesh->SetupAttachment(CapsuleComponent);
	Mesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -TDCombatCharacterMovement::StandardCapsuleHalfHeight), FRotator(0.f, -90.f, 0.f));
	Mesh->AlwaysLoadOnClient = true;
	Mesh->AlwaysLoadOnServer = true;
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	Mesh->bCastDynamicShadow = true;
	Mesh->bAffectDynamicIndirectLighting = true;
	Mesh->PrimaryComponentTick.TickGroup = TG_PrePhysics;
	Mesh->SetCollisionProfileName(TEXT("CharacterMesh"));
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetEnableAnimation(false);

	MoverComponent = CreateDefaultSubobject<UCharacterMoverComponent>(TEXT("MoverComponent"));
	MoverComponent->BackendClass = UMoverStandaloneLiaisonComponent::StaticClass();
	MoverComponent->bAcceptExternalMovement = true;
	MoverComponent->bWarnOnExternalMovement = false;
	MoverComponent->StartingMovementMode = DefaultModeNames::Walking;

	NavMoverComponent = CreateDefaultSubobject<UNavMoverComponent>(TEXT("NavMoverComponent"));
	UAFComponent = CreateDefaultSubobject<UUAFComponent>(TEXT("UAFComponent"));
	CharacterAnimation = CreateDefaultSubobject<UTDCharacterAnimationComponent>(TEXT("CharacterAnimation"));

	CombatAttributes = CreateDefaultSubobject<UTDCombatAttributeSet>(TEXT("CombatAttributes"));
	CombatComponent = CreateDefaultSubobject<UTDCombatComponent>(TEXT("CombatComponent"));
}

UAbilitySystemComponent* ATDCombatCharacter::GetAbilitySystemComponent() const
{
	return CombatComponent;
}

void ATDCombatCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (!MoverComponent)
	{
		return;
	}
	MoverComponent->SetUpdatedComponent(CapsuleComponent);
	MoverComponent->SetPrimaryVisualComponent(Mesh);
}

void ATDCombatCharacter::BeginPlay()
{
	CombatComponent->AddAttributeSetSubobject(CombatAttributes.Get());
	CombatComponent->InitAbilityActorInfo(this, this);
	Super::BeginPlay();

	if (HasAuthority())
	{
		TDCombatCharacterMovement::GrantStartupAbilitiesAndEffects(*CombatComponent, StartupAbilities, StartupEffects, this);
	}

	ApplyMovementSettings();
	LinkAnimationBeforeMovement();
	MoverComponent->OnMovementModeChanged.AddUniqueDynamic(this, &ThisClass::HandleMovementModeChanged);
}

void ATDCombatCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	CombatComponent->InitAbilityActorInfo(this, this);
}

void ATDCombatCharacter::UnPossessed()
{
	Super::UnPossessed();
	CombatComponent->RefreshAbilityActorInfo();
}

FVector ATDCombatCharacter::GetNavAgentLocation() const
{
	const FVector FeetLocation = NavMoverComponent ? NavMoverComponent->GetFeetLocation() : FNavigationSystem::InvalidLocation;
	if (FNavigationSystem::IsValidLocation(FeetLocation))
	{
		return FeetLocation;
	}
	if (!CapsuleComponent)
	{
		return GetActorLocation();
	}
	return CapsuleComponent->GetComponentLocation() - FVector(0.f, 0.f, CapsuleComponent->GetScaledCapsuleHalfHeight());
}

void ATDCombatCharacter::UpdateNavigationRelevance()
{
	if (CapsuleComponent)
	{
		CapsuleComponent->SetCanEverAffectNavigation(bCanAffectNavigationGeneration);
	}
}

bool ATDCombatCharacter::CastDamageSpell(int32 Slot, const FVector& Target)
{
	if (!DamageSpells.IsValidIndex(Slot) || !IsValid(DamageSpells[Slot]))
	{
		return false;
	}
	return CombatComponent->TryCastDamageDefinition(DamageSpells[Slot], Target);
}

bool ATDCombatCharacter::IsMovingOnGround() const
{
	return MoverComponent && MoverComponent->IsOnGround();
}

bool ATDCombatCharacter::IsAirborne() const
{
	return MoverComponent && MoverComponent->IsAirborne();
}

bool ATDCombatCharacter::CanJump() const
{
	if (bIsMovementDisabled || bIsMovementFrozen || !TDCombatCharacterMovement::IsMoverReady(MoverComponent))
	{
		return false;
	}
	return MoverComponent->CanActorJump();
}

bool ATDCombatCharacter::Jump()
{
	if (!CanJump())
	{
		return false;
	}
	return MoverComponent->Jump();
}

void ATDCombatCharacter::SetMaxMoveSpeed(float NewMaxSpeed)
{
	UCommonLegacyMovementSettings* MovementSettings = MoverComponent ? MoverComponent->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>() : nullptr;
	if (!MovementSettings)
	{
		return;
	}
	MovementSettings->MaxSpeed = FMath::Max(NewMaxSpeed, 0.f);
}

float ATDCombatCharacter::GetMaxMoveSpeed() const
{
	const UCommonLegacyMovementSettings* MovementSettings = MoverComponent ? MoverComponent->FindSharedSettings<UCommonLegacyMovementSettings>() : nullptr;
	return MovementSettings ? MovementSettings->MaxSpeed : DefaultMaxMoveSpeed;
}

void ATDCombatCharacter::StopMovementImmediately()
{
	if (!TDCombatCharacterMovement::IsMoverReady(MoverComponent))
	{
		return;
	}
	MoverComponent->QueueInstantMovementEffect(MakeShared<FApplyVelocityEffect>());
}

void ATDCombatCharacter::DisableMovement()
{
	if (bIsMovementDisabled)
	{
		return;
	}
	bIsMovementDisabled = true;
	StopMovementImmediately();
	if (MoverComponent)
	{
		MoverComponent->QueueNextMode(UNullMovementMode::NullModeName);
	}
}

void ATDCombatCharacter::EnableMovement()
{
	if (!bIsMovementDisabled)
	{
		return;
	}
	bIsMovementDisabled = false;
	if (bIsMovementFrozen || !MoverComponent)
	{
		return;
	}
	MoverComponent->QueueNextMode(DefaultModeNames::Falling);
}

void ATDCombatCharacter::AddImpulseVelocity(const FVector& VelocityChange)
{
	if (bIsMovementDisabled || bIsMovementFrozen || !TDCombatCharacterMovement::IsMoverReady(MoverComponent) || VelocityChange.IsNearlyZero())
	{
		return;
	}
	const TSharedPtr<FApplyVelocityEffect> VelocityEffect = MakeShared<FApplyVelocityEffect>();
	VelocityEffect->VelocityToApply = VelocityChange;
	VelocityEffect->bAdditiveVelocity = true;
	if (VelocityChange.Z > 0.f)
	{
		VelocityEffect->ForceMovementMode = DefaultModeNames::Falling;
	}
	MoverComponent->QueueInstantMovementEffect(VelocityEffect);
}

void ATDCombatCharacter::FaceRotationImmediately(const FRotator& NewRotation)
{
	const FRotator UprightRotation(0.f, NewRotation.Yaw, 0.f);
	DesiredFacingDirection = TDCombatCharacterMovement::MakePlanarDirection(UprightRotation);
	SetActorRotation(UprightRotation);
	if (!TDCombatCharacterMovement::IsMoverReady(MoverComponent))
	{
		return;
	}
	const TSharedPtr<FTeleportEffect> RotationEffect = MakeShared<FTeleportEffect>();
	RotationEffect->TargetLocation = GetActorLocation();
	RotationEffect->bUseActorRotation = false;
	RotationEffect->TargetRotation = UprightRotation;
	MoverComponent->QueueInstantMovementEffect(RotationEffect);
}

void ATDCombatCharacter::TeleportPawn(const FVector& NewLocation, const FRotator& NewRotation)
{
	const FRotator UprightRotation(0.f, NewRotation.Yaw, 0.f);
	DesiredFacingDirection = TDCombatCharacterMovement::MakePlanarDirection(UprightRotation);
	if (!TeleportTo(NewLocation, UprightRotation))
	{
		SetActorLocationAndRotation(NewLocation, UprightRotation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (!TDCombatCharacterMovement::IsMoverReady(MoverComponent))
	{
		return;
	}
	const TSharedPtr<FTeleportEffect> TeleportEffect = MakeShared<FTeleportEffect>();
	TeleportEffect->TargetLocation = GetActorLocation();
	TeleportEffect->bUseActorRotation = false;
	TeleportEffect->TargetRotation = UprightRotation;
	MoverComponent->QueueInstantMovementEffect(TeleportEffect);
}

void ATDCombatCharacter::SetDesiredFacingDirection(const FVector& Direction)
{
	DesiredFacingDirection = Direction.GetSafeNormal2D();
}

void ATDCombatCharacter::ClearDesiredFacingDirection()
{
	DesiredFacingDirection = FVector::ZeroVector;
}

void ATDCombatCharacter::SetMovementFrozen(bool bIsFrozen)
{
	if (bIsMovementFrozen == bIsFrozen)
	{
		return;
	}
	bIsMovementFrozen = bIsFrozen;
	if (CharacterAnimation)
	{
		CharacterAnimation->SetAnimationFrozen(bIsFrozen);
	}
	if (!MoverComponent)
	{
		return;
	}

	if (bIsFrozen)
	{
		MovementModeBeforeFreeze = MoverComponent->GetMovementModeName();
		StopMovementImmediately();
		MoverComponent->QueueNextMode(UNullMovementMode::NullModeName);
		return;
	}

	const FName MovementModeToRestore = MovementModeBeforeFreeze;
	MovementModeBeforeFreeze = NAME_None;
	if (bIsMovementDisabled)
	{
		return;
	}
	MoverComponent->QueueNextMode(TDCombatCharacterMovement::IsRestorableMovementMode(MovementModeToRestore) ? MovementModeToRestore : DefaultModeNames::Falling);
}

void ATDCombatCharacter::ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmdResult)
{
	FVector NavMoveIntent = FVector::ZeroVector;
	FVector NavMoveVelocity = FVector::ZeroVector;
	if (NavMoverComponent)
	{
		NavMoverComponent->ConsumeNavMovementData(NavMoveIntent, NavMoveVelocity);
	}
	const FVector PawnMoveIntent = ConsumeMoveIntent();

	FCharacterDefaultInputs& CharacterInputs = InputCmdResult.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
	const AController* OwningController = GetController();
	if (OwningController)
	{
		CharacterInputs.ControlRotation = OwningController->GetControlRotation();
	}

	if (bIsMovementDisabled || bIsMovementFrozen)
	{
		CharacterInputs.SetMoveInput(EMoveInputType::DirectionalIntent, FVector::ZeroVector);
		CharacterInputs.OrientationIntent = FVector::ZeroVector;
		return;
	}

	if (!NavMoveVelocity.IsZero())
	{
		CharacterInputs.SetMoveInput(EMoveInputType::Velocity, NavMoveVelocity);
	}
	else
	{
		CharacterInputs.SetMoveInput(EMoveInputType::DirectionalIntent, (PawnMoveIntent + NavMoveIntent).GetClampedToMaxSize(1.f));
	}

	if (FacingMode == ETDFacingMode::ControlRotation && OwningController)
	{
		CharacterInputs.OrientationIntent = TDCombatCharacterMovement::MakePlanarDirection(CharacterInputs.ControlRotation);
		return;
	}

	const FVector MoveInput = CharacterInputs.GetMoveInput();
	const bool bHasMoveInput = MoveInput.Size2D() >= TDCombatCharacterMovement::MinimumOrientationInputSize;
	if (FacingMode == ETDFacingMode::MovementDirection && bHasMoveInput)
	{
		DesiredFacingDirection = FVector::ZeroVector;
		CharacterInputs.OrientationIntent = MoveInput.GetSafeNormal2D();
		return;
	}
	CharacterInputs.OrientationIntent = DesiredFacingDirection;
}

void ATDCombatCharacter::HandleMovementModeChanged(const FName& PreviousMovementModeName, const FName& NewMovementModeName)
{
}

void ATDCombatCharacter::ApplyMovementSettings()
{
	UCommonLegacyMovementSettings* MovementSettings = MoverComponent ? MoverComponent->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>() : nullptr;
	if (!MovementSettings)
	{
		return;
	}
	MovementSettings->MaxSpeed = DefaultMaxMoveSpeed;
	MovementSettings->Acceleration = MoveAcceleration;
	MovementSettings->Deceleration = MoveDeceleration;
	MovementSettings->TurningRate = TurningRate;
}

void ATDCombatCharacter::LinkAnimationBeforeMovement()
{
	UMoverStandaloneLiaisonComponent* MovementBackend = FindComponentByClass<UMoverStandaloneLiaisonComponent>();
	FTickFunction* SimulateMovementTick = MovementBackend ? MovementBackend->FindTickFunction(EMoverTickPhase::SimulateMovement) : nullptr;
	if (!SimulateMovementTick || !UAFComponent || !UAFComponent->GetSystemReference().IsValid())
	{
		return;
	}
	UAFComponent->AddSubsequent(MovementBackend, *SimulateMovementTick, TDCombatCharacterMovement::GetAnimationPrePhysicsEventName());
	if (CharacterAnimation)
	{
		UAFComponent->AddComponentPrerequisite(CharacterAnimation, TDCombatCharacterMovement::GetAnimationPrePhysicsEventName());
	}
}

FVector ATDCombatCharacter::ConsumeMoveIntent()
{
	return Internal_ConsumeMovementInputVector().GetClampedToMaxSize(1.f);
}
