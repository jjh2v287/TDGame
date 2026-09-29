// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/TDGameCharacter.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Characters/TDCapsuleModifierComponent.h"
#include "Characters/TDCharacterAnimationComponent.h"
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
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/NavMoverComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/Material.h"
#include "Engine/World.h"
#include "MovementMode.h"
#include "MoverTypes.h"

ATDGameCharacter::ATDGameCharacter()
{
	FTDCombatStats PlayerStats;
	PlayerStats.TeamId = 1;
	CombatComponent->SetStats(PlayerStats);

	// Set size for player capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -96.f));

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> DefaultPlayerMesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (DefaultPlayerMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMeshAsset(DefaultPlayerMesh.Object);
	}

	// Don't rotate character to camera direction
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	DefaultMaxMoveSpeed = 600.f;
	TurningRate = 640.f;
	FacingMode = ETDFacingMode::MovementDirection;
	if (FNavMovementProperties* NavProps = GetNavMoverComponent()->GetNavMovementProperties())
	{
		NavProps->bUseAccelerationForPaths = true;
	}

	LocomotionBlendSpace = TSoftObjectPtr<UBlendSpace>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run.BS_Idle_Walk_Run")));
	JumpStartAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Jump.MM_Jump")));
	FallLoopAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Fall_Loop.MM_Fall_Loop")));
	LandAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Land.MM_Land")));

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
		const TCHAR* SpellNames[] =
		{
			TEXT("Fireball"), TEXT("Blizzard"), TEXT("Mine"), TEXT("Shockwave"), TEXT("Meteor"),
			TEXT("DelayedHoming"), TEXT("ThunderCage"), TEXT("VenomBloom"), TEXT("AstralLances"), TEXT("PhoenixDive")
		};
		for (const TCHAR* SpellName : SpellNames)
		{
			const FString AssetPath = FString::Printf(TEXT("/Game/Combat/MegaMagic/DA_TD%s.DA_TD%s"), SpellName, SpellName);
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
			TDDamageExamples::CreateMegaMagicExamples(this, Examples);
			for (UTDDamageDefinition* Definition : Examples)
			{
				DamageSpells.Add(Definition);
			}
		}
	}

	Super::BeginPlay();

	ApplyAnimationSettings();
	GrantDefaultActionAbilities();
	CombatComponent->OnDeath.AddUObject(this, &ThisClass::HandleDeath);
	RefreshJumpStateTag(IsAirborne());
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

bool ATDGameCharacter::CanStartJump() const
{
	if (IsRollPlaying())
	{
		return false;
	}

	if (!CombatComponent->IsAlive())
	{
		return false;
	}

	return CanJump();
}

void ATDGameCharacter::HandleMovementModeChanged(const FName& PreviousMovementModeName, const FName& NewMovementModeName)
{
	Super::HandleMovementModeChanged(PreviousMovementModeName, NewMovementModeName);

	const UBaseMovementMode* NewMovementMode = GetMoverComponent() ? GetMoverComponent()->FindMovementModeByName(NewMovementModeName) : nullptr;
	RefreshJumpStateTag(NewMovementMode && NewMovementMode->HasGameplayTag(Mover_IsInAir, true));
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

	const UAnimSequence* RollSequence = RollAnimation.Get();
	if (!RollSequence || !CharacterAnimation)
	{
		return false;
	}

	return CharacterAnimation->IsPlayingAction(RollSequence);
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
	if (!CombatComponent->IsAlive() || CombatComponent->IsFrozen() || RollAnimation.IsNull())
	{
		return false;
	}

	if (IsRollPlaying())
	{
		return false;
	}

	if (!IsMovingOnGround())
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

float ATDGameCharacter::PlayRollAnimation(const FVector& RollDirection)
{
	UAnimSequence* RollSequence = RollAnimation.LoadSynchronous();
	if (!RollSequence || !CharacterAnimation || CharacterAnimation->IsPlayingAction(RollSequence))
	{
		return 0.f;
	}

	if (CapsuleModifierComponent)
	{
		CapsuleModifierComponent->SetRollModifierEnabled(true);
	}

	FVector DesiredRollDirection = RollDirection;
	DesiredRollDirection.Z = 0.f;
	if (!DesiredRollDirection.IsNearlyZero())
	{
		FaceRotationImmediately(DesiredRollDirection.Rotation());
	}

	const float RollDuration = CharacterAnimation->PlayAction(RollSequence, 1.f, 0.f, 0.f, true);
	if (RollDuration <= 0.f)
	{
		EndRollAnimation();
		return 0.f;
	}

	return RollDuration;
}

void ATDGameCharacter::EndRollAnimation()
{
	if (CapsuleModifierComponent)
	{
		CapsuleModifierComponent->SetRollModifierEnabled(false);
	}
}

void ATDGameCharacter::HandleActionAnimationEnded(const UAnimSequence* Animation, bool bWasInterrupted)
{
	if (!Animation || Animation != RollAnimation.Get())
	{
		return;
	}

	EndRollAnimation();
}

void ATDGameCharacter::HandleDeath(const FTDDamageContext& Context)
{
	if (CapsuleModifierComponent)
	{
		CapsuleModifierComponent->ResetModifiers();
	}

	DisableMovement();
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

void ATDGameCharacter::ApplyAnimationSettings()
{
	if (!CharacterAnimation)
	{
		return;
	}

	if (UBlendSpace* BlendSpace = LocomotionBlendSpace.LoadSynchronous())
	{
		CharacterAnimation->UseLocomotionBlendSpace(BlendSpace);
	}

	FTDAirborneClipSet AirborneClips;
	AirborneClips.JumpStart = JumpStartAnimation.LoadSynchronous();
	AirborneClips.FallLoop = FallLoopAnimation.LoadSynchronous();
	AirborneClips.Land = LandAnimation.LoadSynchronous();
	CharacterAnimation->UseAirborneClips(AirborneClips);

	CharacterAnimation->OnActionAnimationEnded.AddUObject(this, &ThisClass::HandleActionAnimationEnded);
}

void ATDGameCharacter::RefreshJumpStateTag(const bool bIsInAir) const
{
	if (bIsInAir)
	{
		CombatComponent->AddLooseGameplayTag(TDGameplayTags::State_Jumping);
		return;
	}

	CombatComponent->RemoveLooseGameplayTag(TDGameplayTags::State_Jumping);
}
