// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/TDGameCharacter.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Characters/TDCapsuleModifierComponent.h"
#include "Combat/GAS/Abilities/TDCombatActionAbility.h"
#include "Combat/GAS/Abilities/TDPlayerMovementAbilities.h"
#include "Combat/GAS/Abilities/TDReactionAbility.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageExamples.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "GameplayAbilitySpec.h"
#include "UObject/ConstructorHelpers.h"
#include "Camera/CameraComponent.h"
#include "Components/DecalComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/Material.h"
#include "Engine/World.h"

ATDGameCharacter::ATDGameCharacter()
{
	FTDCombatStats PlayerStats;
	PlayerStats.TeamId = 1;
	CombatComponent->SetStats(PlayerStats);

	// Set size for player capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// Don't rotate character to camera direction
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 640.f, 0.f);
	GetCharacterMovement()->bConstrainToPlane = true;
	GetCharacterMovement()->bSnapToPlaneAtStart = true;

	// Create the camera boom component
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));

	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->TargetArmLength = 800.f;
	CameraBoom->SetRelativeRotation(FRotator(-60.f, 0.f, 0.f));
	CameraBoom->bDoCollisionTest = false;

	// Create the camera component
	TopDownCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));

	TopDownCameraComponent->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCameraComponent->bUsePawnControlRotation = false;

	CapsuleModifierComponent = CreateDefaultSubobject<UTDCapsuleModifierComponent>(TEXT("CapsuleModifierComponent"));
	SkillComponent = CreateDefaultSubobject<UTDSkillComponent>(TEXT("SkillComponent"));

	// Activate ticking in order to update the cursor every frame.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ATDGameCharacter::BeginPlay()
{
	if (DamageSpells.IsEmpty())
	{
		const TCHAR* SpellNames[] = { TEXT("Fireball"), TEXT("Blizzard"), TEXT("Mine"), TEXT("Shockwave"), TEXT("Meteor"), TEXT("DelayedHoming") };
		for (const TCHAR* SpellName : SpellNames)
		{
			const FString AssetPath = FString::Printf(TEXT("/Game/Combat/Examples/DA_TD%s.DA_TD%s"), SpellName, SpellName);
			UTDDamageDefinition* Definition = LoadObject<UTDDamageDefinition>(nullptr, *AssetPath, nullptr, LOAD_NoWarn);
			if (!Definition)
			{
				DamageSpells.Reset();
				break;
			}
			DamageSpells.Add(Definition);
		}

		if (DamageSpells.IsEmpty())
		{
			TArray<UTDDamageDefinition*> Examples;
			TDDamageExamples::CreateExamples(this, Examples);
			for (UTDDamageDefinition* Definition : Examples)
			{
				DamageSpells.Add(Definition);
			}
		}
	}
	Super::BeginPlay();

	GrantDefaultActionAbilities();
	CombatComponent->OnDeath.AddUObject(this, &ThisClass::HandleDeath);
	RefreshJumpStateTag();
}

void ATDGameCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

	// stub
}

FGenericTeamId ATDGameCharacter::GetGenericTeamId() const
{
	return FGenericTeamId(static_cast<uint8>(CombatComponent->GetStats().TeamId));
}

bool ATDGameCharacter::CanJumpInternal_Implementation() const
{
	if (IsRollPlaying())
	{
		return false;
	}

	if (!CombatComponent->IsAlive())
	{
		return false;
	}

	return Super::CanJumpInternal_Implementation();
}

void ATDGameCharacter::OnMovementModeChanged(const EMovementMode PrevMovementMode, const uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);
	RefreshJumpStateTag();
}

float ATDGameCharacter::GetDesiredAttackRange() const
{
	if (!SkillComponent)
	{
		return 0.f;
	}

	const FGameplayTag PreferredActionTag = SkillComponent->HasActionDefinition(TDGameplayTags::Action_Attack_Primary_01)
		? TDGameplayTags::Action_Attack_Primary_01
		: TDGameplayTags::Action_Attack_Primary;
	return SkillComponent->GetActionRange(PreferredActionTag);
}

bool ATDGameCharacter::IsAttackReady() const
{
	if (!SkillComponent)
	{
		return false;
	}

	int32 ComboStep = 0;
	const FGameplayTag AttackTag = SkillComponent->ResolvePrimaryActionTag(ComboStep);
	return SkillComponent->CanUseAction(AttackTag);
}

bool ATDGameCharacter::ExecutePrimaryAttack(AActor* TargetActor)
{
	if (!TargetActor || !SkillComponent)
	{
		return false;
	}

	int32 ComboStep = 0;
	const FGameplayTag AttackTag = SkillComponent->ResolvePrimaryActionTag(ComboStep);
	if (!AttackTag.IsValid() || !SkillComponent->CanUseAction(AttackTag))
	{
		return false;
	}

	if (ActivateCombatAbility(AttackTag, TargetActor))
	{
		return true;
	}

	return SkillComponent->BufferCombatInput(TDGameplayTags::Action_Attack_Primary, TargetActor);
}

bool ATDGameCharacter::ExecuteSkillQ(AActor* TargetActor)
{
	if (ActivateCombatAbility(TDGameplayTags::Action_Skill_Q, TargetActor))
	{
		return true;
	}

	return SkillComponent ? SkillComponent->BufferCombatInput(TDGameplayTags::Action_Skill_Q, TargetActor) : false;
}

bool ATDGameCharacter::ExecuteSkillE(AActor* TargetActor)
{
	if (ActivateCombatAbility(TDGameplayTags::Action_Skill_E, TargetActor))
	{
		return true;
	}

	return SkillComponent ? SkillComponent->BufferCombatInput(TDGameplayTags::Action_Skill_E, TargetActor) : false;
}

bool ATDGameCharacter::TryStartRoll(const FVector& RollDirection)
{
	if (!CanStartRoll())
	{
		return false;
	}

	PendingRollDirection = RollDirection;
	const bool bActivated = ActivateCombatAbility(TDGameplayTags::Action_Roll);
	if (!bActivated)
	{
		PendingRollDirection = FVector::ForwardVector;
	}
	return bActivated;
}

bool ATDGameCharacter::TryStartJumpAbility()
{
	return ActivateCombatAbility(TDGameplayTags::Action_Jump);
}

bool ATDGameCharacter::IsRollPlaying() const
{
	if (CombatComponent->HasMatchingGameplayTag(TDGameplayTags::State_Rolling))
	{
		return true;
	}

	if (RollMontage.IsNull())
	{
		return false;
	}

	UAnimMontage* RollMontageAsset = ResolveMontage(RollMontage);
	if (!RollMontageAsset)
	{
		return false;
	}

	if (const UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		return AnimInstance->Montage_IsPlaying(RollMontageAsset);
	}

	return false;
}

bool ATDGameCharacter::ActivateCombatAbility(const FGameplayTag AbilityTag, AActor* TargetActor)
{
	if (!AbilityTag.IsValid() || !CombatComponent->IsAlive())
	{
		return false;
	}

	if (SkillComponent)
	{
		SkillComponent->SetCombatTarget(TargetActor);
	}

	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(AbilityTag);
	return CombatComponent->TryActivateAbilitiesByTag(AbilityTags, true);
}

bool ATDGameCharacter::CanStartRoll() const
{
	const UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	if (!CombatComponent->IsAlive() || RollMontage.IsNull() || !MovementComponent)
	{
		return false;
	}

	if (IsRollPlaying())
	{
		return false;
	}

	if (bPressedJump || !MovementComponent->IsMovingOnGround())
	{
		return false;
	}

	if (CombatComponent->HasMatchingGameplayTag(TDGameplayTags::State_Attacking) ||
		CombatComponent->HasMatchingGameplayTag(TDGameplayTags::State_Skill) ||
		CombatComponent->HasMatchingGameplayTag(TDGameplayTags::State_Stunned))
	{
		return false;
	}

	if (CombatComponent->GetCurrentStamina() < RollStaminaCost)
	{
		return false;
	}

	return true;
}

UAnimMontage* ATDGameCharacter::ResolveMontage(const TSoftObjectPtr<UAnimMontage>& MontageReference) const
{
	if (MontageReference.IsNull())
	{
		return nullptr;
	}

	return MontageReference.LoadSynchronous();
}

FVector ATDGameCharacter::ConsumePendingRollDirection()
{
	FVector RollDirection = PendingRollDirection;
	RollDirection.Z = 0.f;
	if (RollDirection.IsNearlyZero())
	{
		RollDirection = GetActorForwardVector();
		RollDirection.Z = 0.f;
	}

	PendingRollDirection = FVector::ForwardVector;
	return RollDirection.GetSafeNormal();
}

float ATDGameCharacter::PlayRollMontageAbility(const FVector& RollDirection)
{
	UAnimMontage* RollMontageAsset = ResolveMontage(RollMontage);
	if (!RollMontageAsset)
	{
		return 0.f;
	}

	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || AnimInstance->Montage_IsPlaying(RollMontageAsset))
	{
		return 0.f;
	}

	FVector DesiredRollDirection = RollDirection;
	DesiredRollDirection.Z = 0.f;
	if (!DesiredRollDirection.IsNearlyZero())
	{
		SetActorRotation(DesiredRollDirection.Rotation());
	}

	if (CapsuleModifierComponent)
	{
		CapsuleModifierComponent->SetRollModifierEnabled(true);
	}

	const float MontageDuration = AnimInstance->Montage_Play(RollMontageAsset);
	if (MontageDuration <= 0.f)
	{
		EndRollMontageAbility();
		return 0.f;
	}

	FOnMontageEnded MontageEndedDelegate;
	MontageEndedDelegate.BindUObject(this, &ThisClass::HandleRollMontageEnded);
	AnimInstance->Montage_SetEndDelegate(MontageEndedDelegate, RollMontageAsset);
	return MontageDuration;
}

void ATDGameCharacter::EndRollMontageAbility()
{
	if (CapsuleModifierComponent)
	{
		CapsuleModifierComponent->SetRollModifierEnabled(false);
	}
}

void ATDGameCharacter::HandleRollMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == ResolveMontage(RollMontage))
	{
		EndRollMontageAbility();
	}
}

void ATDGameCharacter::HandleDeath(const FTDDamageContext& Context)
{
	if (CapsuleModifierComponent)
	{
		CapsuleModifierComponent->ResetModifiers();
	}

	GetCharacterMovement()->DisableMovement();
	SetActorEnableCollision(false);
	if (SkillComponent)
	{
		SkillComponent->ResetPrimaryCombo();
		SkillComponent->SetCombatTarget(nullptr);
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTDActorDeathMessage DeathMessage;
	DeathMessage.DeadActor = this;
	DeathMessage.Killer = Context.Caster.Get();
	DeathMessage.DeathLocation = GetActorLocation();
	UGameplayMessageSubsystem::Get(World).BroadcastMessage(TDGameplayTags::Event_Actor_Death, DeathMessage);
}

void ATDGameCharacter::GrantDefaultActionAbilities()
{
	if (!bGrantDefaultActionAbilities || !HasAuthority() || bHasGrantedDefaultActionAbilities)
	{
		return;
	}

	TArray<TSubclassOf<UGameplayAbility>> AbilityClasses;
	if (StartupAbilities.IsEmpty())
	{
		AbilityClasses =
		{
			UTDPlayerPrimaryAttack01Ability::StaticClass(),
			UTDPlayerPrimaryAttack02Ability::StaticClass(),
			UTDPlayerPrimaryAttack03Ability::StaticClass(),
			UTDPlayerSkillQAbility::StaticClass(),
			UTDPlayerSkillEAbility::StaticClass(),
			UTDReactionHitAbility::StaticClass(),
			UTDReactionDeathAbility::StaticClass(),
			UTDPlayerRollAbility::StaticClass(),
			UTDPlayerJumpAbility::StaticClass()
		};
	}
	else if (StartupAbilities.Contains(UTDPlayerPrimaryAttackAbility::StaticClass()))
	{
		AbilityClasses.Add(UTDPlayerPrimaryAttack01Ability::StaticClass());
		AbilityClasses.Add(UTDPlayerPrimaryAttack02Ability::StaticClass());
		AbilityClasses.Add(UTDPlayerPrimaryAttack03Ability::StaticClass());
	}

	AbilityClasses.AddUnique(UTDReactionHitAbility::StaticClass());
	AbilityClasses.AddUnique(UTDReactionDeathAbility::StaticClass());

	const int32 AbilityLevel = CombatComponent->GetStats().Level;
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : AbilityClasses)
	{
		if (!AbilityClass || StartupAbilities.Contains(AbilityClass))
		{
			continue;
		}
		CombatComponent->GiveAbility(FGameplayAbilitySpec(AbilityClass, AbilityLevel, INDEX_NONE, this));
	}

	bHasGrantedDefaultActionAbilities = true;
}

void ATDGameCharacter::RefreshJumpStateTag() const
{
	if (GetCharacterMovement() && GetCharacterMovement()->IsFalling())
	{
		CombatComponent->AddLooseGameplayTag(TDGameplayTags::State_Jumping);
		return;
	}

	CombatComponent->RemoveLooseGameplayTag(TDGameplayTags::State_Jumping);
}
