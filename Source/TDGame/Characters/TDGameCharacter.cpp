// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/TDGameCharacter.h"
#include "Abilities/GameplayAbility.h"
#include "Algo/AllOf.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Characters/TDCapsuleModifierComponent.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Combat/GAS/Abilities/TDCombatActionAbility.h"
#include "Combat/GAS/Abilities/TDPlayerMovementAbilities.h"
#include "Combat/GAS/Abilities/TDReactionAbility.h"
#include "Combat/Skills/TDCombatStyleDefinition.h"
#include "Combat/Skills/TDPlayerCombatCatalog.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageExamples.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Core/TDGameplayMessages.h"
#include "Core/TDGameplayTags.h"
#include "GameplayAbilitySpec.h"
#include "UObject/ConstructorHelpers.h"
#include "Camera/CameraComponent.h"
#include "Components/DecalComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/NavMoverComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/Material.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "MovementMode.h"
#include "MoverTypes.h"
#include "TDGame.h"

namespace
{
	constexpr float TDGameCharacterMinimumPassagePaceScale = 0.3f;
	constexpr int32 TDGameCharacterSwordComboStepCount = 4;
	constexpr float TDGameCharacterSwordComboResetSeconds = 1.6f;

	void ConfigureTDWeaponMeshComponent(UStaticMeshComponent& WeaponMeshComponent, const FTransform& RelativeTransform)
	{
		WeaponMeshComponent.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WeaponMeshComponent.SetGenerateOverlapEvents(false);
		WeaponMeshComponent.SetCanEverAffectNavigation(false);
		WeaponMeshComponent.SetRelativeTransform(RelativeTransform);
	}

	void AttachTDWeaponMeshComponent(UStaticMeshComponent* WeaponMeshComponent, USkeletalMeshComponent* CharacterMesh, const FName SocketName, const FTransform& RelativeTransform)
	{
		if (!WeaponMeshComponent || !CharacterMesh)
		{
			return;
		}
		WeaponMeshComponent->AttachToComponent(CharacterMesh, FAttachmentTransformRules::KeepRelativeTransform, SocketName);
		WeaponMeshComponent->SetRelativeTransform(RelativeTransform);
	}

	void AssignTDWeaponStaticMesh(UStaticMeshComponent* WeaponMeshComponent, const TSoftObjectPtr<UStaticMesh>& WeaponMesh)
	{
		UStaticMesh* LoadedMesh = WeaponMesh.Get();
		if (!WeaponMeshComponent || !LoadedMesh)
		{
			return;
		}
		WeaponMeshComponent->SetStaticMesh(LoadedMesh);
	}

	TArray<FSoftObjectPath> MakeTDDefaultDamageSpellPaths()
	{
		const TCHAR* SpellNames[] =
		{
			TEXT("Fireball"), TEXT("Blizzard"), TEXT("Mine"), TEXT("Shockwave"), TEXT("Meteor"),
			TEXT("DelayedHoming"), TEXT("ThunderCage"), TEXT("VenomBloom"), TEXT("AstralLances"), TEXT("PhoenixDive")
		};

		TArray<FSoftObjectPath> SpellPaths;
		for (const TCHAR* SpellName : SpellNames)
		{
			SpellPaths.Add(FSoftObjectPath(FString::Printf(TEXT("/Game/Combat/MegaMagic/DA_TD%s.DA_TD%s"), SpellName, SpellName)));
		}
		return SpellPaths;
	}
}

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
	RollAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Roll/A_Roll_IdleFwd.A_Roll_IdleFwd")));
	bBlockMoveInputWhileStunned = true;

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
	SkillComponent->SetUsesHitReactionThresholds(true);

	SwordMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Sword.SM_Sword")));
	ShieldMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Shield.SM_Shield")));
	SwordRelativeTransform = FTransform(FRotator(-15.888f, 94.203f, -83.734f), FVector(-5.052f, 9.119f, 25.857f));
	ShieldRelativeTransform = FTransform(FRotator(-1.016f, 6.03f, 101.973f), FVector(-5.991f, 9.108f, -0.007f), FVector(0.7f));

	SwordMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SwordMesh"));
	SwordMeshComponent->SetupAttachment(GetMesh(), SwordSocketName);
	ConfigureTDWeaponMeshComponent(*SwordMeshComponent, SwordRelativeTransform);

	ShieldMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShieldMesh"));
	ShieldMeshComponent->SetupAttachment(GetMesh(), ShieldSocketName);
	ConfigureTDWeaponMeshComponent(*ShieldMeshComponent, ShieldRelativeTransform);

	// Activate ticking in order to update the cursor every frame.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ATDGameCharacter::BeginPlay()
{
	if (DamageSpells.IsEmpty())
	{
		RequestDefaultDamageSpells();
	}
	ApplyDefaultCombatStyle();

	Super::BeginPlay();

	if (CharacterAnimation)
	{
		CharacterAnimation->OnActionAnimationEnded.AddUObject(this, &ThisClass::HandleActionAnimationEnded);
	}
	RequestAnimationAssets();
	GrantDefaultActionAbilities();
	CombatComponent->OnDamaged.AddUObject(this, &ThisClass::HandleDamaged);
	CombatComponent->OnDeath.AddUObject(this, &ThisClass::HandleDeath);
	RefreshJumpStateTag(IsAirborne());
	AttachWeaponMeshes();
	RequestWeaponMeshes();
}

void ATDGameCharacter::ApplyDefaultCombatStyle()
{
	if (!SkillComponent || SkillComponent->GetSkillSet())
	{
		return;
	}
	UTDCombatStyleDefinition* SwordShieldStyle = TDPlayerCombatCatalog::CreateSwordShieldStyle(this);
	if (!SwordShieldStyle)
	{
		return;
	}
	SkillComponent->SetSkillSet(SwordShieldStyle);
	SkillComponent->SetPrimaryComboSettings(TDGameCharacterSwordComboStepCount, TDGameCharacterSwordComboResetSeconds);
}

void ATDGameCharacter::AttachWeaponMeshes()
{
	AttachTDWeaponMeshComponent(SwordMeshComponent, GetMesh(), SwordSocketName, SwordRelativeTransform);
	AttachTDWeaponMeshComponent(ShieldMeshComponent, GetMesh(), ShieldSocketName, ShieldRelativeTransform);
}

void ATDGameCharacter::RequestWeaponMeshes()
{
	if (!FApp::CanEverRender())
	{
		return;
	}

	TArray<FSoftObjectPath> WeaponMeshPaths;
	const FSoftObjectPath WeaponMeshCandidatePaths[] = { SwordMesh.ToSoftObjectPath(), ShieldMesh.ToSoftObjectPath() };
	for (const FSoftObjectPath& WeaponMeshPath : WeaponMeshCandidatePaths)
	{
		if (!WeaponMeshPath.IsNull())
		{
			WeaponMeshPaths.Add(WeaponMeshPath);
		}
	}
	if (WeaponMeshPaths.IsEmpty())
	{
		return;
	}

	const bool bAreWeaponMeshesResident = Algo::AllOf(WeaponMeshPaths, [](const FSoftObjectPath& WeaponMeshPath) { return WeaponMeshPath.ResolveObject() != nullptr; });
	if (bAreWeaponMeshesResident)
	{
		ApplyWeaponMeshes();
		return;
	}
	WeaponMeshesHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(WeaponMeshPaths, FStreamableDelegate::CreateUObject(this, &ThisClass::ApplyWeaponMeshes));
	if (!WeaponMeshesHandle.IsValid())
	{
		ApplyWeaponMeshes();
	}
}

void ATDGameCharacter::ApplyWeaponMeshes()
{
	AssignTDWeaponStaticMesh(SwordMeshComponent, SwordMesh);
	AssignTDWeaponStaticMesh(ShieldMeshComponent, ShieldMesh);
}

void ATDGameCharacter::HandleDamaged(const FTDDamageResult& Result, const FTDDamageContext& Context)
{
	if (Result.AppliedDamage <= 0.f || Result.bWasKilled || Result.Delivery == ETDDamageDelivery::Status || !CombatComponent->IsAlive())
	{
		return;
	}
	if (Result.DamageRatio < LightKnockbackRatio)
	{
		return;
	}
	const bool bIsHeavyHit = Result.bWasCritical || Result.DamageRatio >= HeavyKnockbackRatio;
	const FVector KnockbackDirection = ResolveHitPlanarDirection(Result);
	ApplyKnockback(KnockbackDirection * (bIsHeavyHit ? HeavyKnockbackSpeed : LightKnockbackSpeed), bIsHeavyHit ? HeavyKnockbackSeconds : LightKnockbackSeconds);
}

void ATDGameCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

	// stub
}

void ATDGameCharacter::UnPossessed()
{
	Super::UnPossessed();
	SetPassagePaceScale(1.f);
}

void ATDGameCharacter::SetPassagePaceScale(float Scale)
{
	SetMaxMoveSpeed(DefaultMaxMoveSpeed * FMath::Clamp(Scale, TDGameCharacterMinimumPassagePaceScale, 1.f));
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
	if (!CombatComponent->IsAlive() || CombatComponent->IsFrozen() || RollAnimation.Get() == nullptr)
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
	UAnimSequence* RollSequence = RollAnimation.Get();
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
	StartRagdoll(ComputeKillVelocity(CombatComponent->GetLastDamageResult()));

	UWorld* World = GetWorld();
	if (!World || !UGameplayMessageSubsystem::HasInstance(World))
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
			UTDPlayerPrimaryAttack04Ability::StaticClass(),
			UTDPlayerSkillQAbility::StaticClass(),
			UTDPlayerSkillEAbility::StaticClass(),
			UTDReactionHitAbility::StaticClass(),
			UTDReactionHeavyHitAbility::StaticClass(),
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
		AbilityClasses.Add(UTDPlayerPrimaryAttack04Ability::StaticClass());
	}

	AbilityClasses.AddUnique(UTDReactionHitAbility::StaticClass());
	AbilityClasses.AddUnique(UTDReactionHeavyHitAbility::StaticClass());
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

void ATDGameCharacter::RequestDefaultDamageSpells()
{
	TArray<FSoftObjectPath> SpellPaths = MakeTDDefaultDamageSpellPaths();
	if (Algo::AllOf(SpellPaths, [](const FSoftObjectPath& SpellPath) { return SpellPath.ResolveObject() != nullptr; }))
	{
		HandleDefaultDamageSpellsLoaded();
		return;
	}

	DefaultDamageSpellsHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		SpellPaths, FStreamableDelegate::CreateUObject(this, &ThisClass::HandleDefaultDamageSpellsLoaded));
	if (!DefaultDamageSpellsHandle.IsValid())
	{
		HandleDefaultDamageSpellsLoaded();
	}
}

void ATDGameCharacter::HandleDefaultDamageSpellsLoaded()
{
	if (!DamageSpells.IsEmpty())
	{
		return;
	}

	TArray<UTDDamageDefinition*> LoadedSpells;
	for (const FSoftObjectPath& SpellPath : MakeTDDefaultDamageSpellPaths())
	{
		UTDDamageDefinition* Definition = Cast<UTDDamageDefinition>(SpellPath.ResolveObject());
		if (!Definition)
		{
			LoadedSpells.Reset();
			break;
		}
		LoadedSpells.Add(Definition);
	}

#if WITH_EDITOR
	if (LoadedSpells.IsEmpty())
	{
		TDDamageExamples::CreateMegaMagicExamples(this, LoadedSpells);
	}
#endif

	if (LoadedSpells.IsEmpty())
	{
		UE_LOG(LogTDGame, Warning, TEXT("MegaMagic spell assets were not found; the player has no default damage spells."));
		return;
	}

	for (UTDDamageDefinition* Definition : LoadedSpells)
	{
		DamageSpells.Add(Definition);
	}
}

void ATDGameCharacter::GatherAnimationAssetPaths(TArray<FSoftObjectPath>& OutPaths) const
{
	const FSoftObjectPath AnimationAssetPaths[] =
	{
		LocomotionBlendSpace.ToSoftObjectPath(),
		JumpStartAnimation.ToSoftObjectPath(),
		FallLoopAnimation.ToSoftObjectPath(),
		LandAnimation.ToSoftObjectPath(),
		RollAnimation.ToSoftObjectPath()
	};
	for (const FSoftObjectPath& AnimationAssetPath : AnimationAssetPaths)
	{
		if (!AnimationAssetPath.IsNull())
		{
			OutPaths.Add(AnimationAssetPath);
		}
	}
}

void ATDGameCharacter::RequestAnimationAssets()
{
	TArray<FSoftObjectPath> AnimationPaths;
	GatherAnimationAssetPaths(AnimationPaths);
	if (AnimationPaths.IsEmpty())
	{
		ApplyAnimationSettings();
		return;
	}

	const bool bAreAnimationsResident = Algo::AllOf(AnimationPaths, [](const FSoftObjectPath& AnimationPath) { return AnimationPath.ResolveObject() != nullptr; });
	FStreamableDelegate OnAnimationsLoaded = bAreAnimationsResident ? FStreamableDelegate() : FStreamableDelegate::CreateUObject(this, &ThisClass::ApplyAnimationSettings);
	AnimationAssetsHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(AnimationPaths, MoveTemp(OnAnimationsLoaded), FStreamableManager::AsyncLoadHighPriority);
	if (bAreAnimationsResident || !AnimationAssetsHandle.IsValid())
	{
		ApplyAnimationSettings();
	}
}

void ATDGameCharacter::ApplyAnimationSettings()
{
	if (!CharacterAnimation)
	{
		return;
	}

	if (UBlendSpace* BlendSpace = LocomotionBlendSpace.Get())
	{
		CharacterAnimation->UseLocomotionBlendSpace(BlendSpace);
	}

	FTDAirborneClipSet AirborneClips;
	AirborneClips.JumpStart = JumpStartAnimation.Get();
	AirborneClips.FallLoop = FallLoopAnimation.Get();
	AirborneClips.Land = LandAnimation.Get();
	CharacterAnimation->UseAirborneClips(AirborneClips);
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
