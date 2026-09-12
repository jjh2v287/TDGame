#include "Combat/GAS/Abilities/TDCombatActionAbility.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/GAS/TDCombatGameplayEffects.h"
#include "Combat/Skills/TDCombatActionTypes.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDCombatLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/TDGameplayTags.h"
#include "GameFramework/Character.h"

namespace
{
UAnimInstance* GetAvatarAnimInstance(const AActor* AvatarActor)
{
	if (!AvatarActor)
	{
		return nullptr;
	}

	if (const ACharacter* Character = Cast<ACharacter>(AvatarActor))
	{
		if (USkeletalMeshComponent* MeshComponent = Character->GetMesh())
		{
			return MeshComponent->GetAnimInstance();
		}
	}

	if (USkeletalMeshComponent* MeshComponent = AvatarActor->FindComponentByClass<USkeletalMeshComponent>())
	{
		return MeshComponent->GetAnimInstance();
	}

	return nullptr;
}
}

UTDCombatActionAbility::UTDCombatActionAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

bool UTDCombatActionAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const UTDSkillComponent* SkillComponent = AvatarActor ? AvatarActor->FindComponentByClass<UTDSkillComponent>() : nullptr;
	if (!SkillComponent)
	{
		return false;
	}

	const bool bUsesPrimaryComboResolution = bUsePrimaryComboResolution || PrimaryComboStep > 0;
	FGameplayTag ActionTag = CombatActionTag;
	int32 ComboStep = 0;
	if (bUsesPrimaryComboResolution)
	{
		ActionTag = SkillComponent->ResolvePrimaryActionTag(ComboStep);
		if (PrimaryComboStep > 0 && (ActionTag != CombatActionTag || ComboStep != PrimaryComboStep))
		{
			return false;
		}
	}

	const UTDCombatComponent* CombatComponent = Cast<UTDCombatComponent>(ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr);
	const bool bAllowActiveStateContinuation = bUsesPrimaryComboResolution && ComboStep > 1;
	if (!bAllowActiveStateContinuation && CombatComponent && ActiveStateTag.IsValid()
		&& CombatComponent->HasMatchingGameplayTag(ActiveStateTag))
	{
		return false;
	}

	return ActionTag.IsValid() && SkillComponent->HasActionDefinition(ActionTag) && SkillComponent->CanUseAction(ActionTag);
}

void UTDCombatActionAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UTDCombatComponent* CombatComponent = GetCombatComponent();
	UTDSkillComponent* SkillComponent = GetSkillComponent();
	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!CombatComponent || !SkillComponent || !AvatarActor)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	int32 ComboStep = 0;
	ActiveCombatActionTag = ResolveRequestedCombatActionTag(ComboStep);
	if (!ActiveCombatActionTag.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FTDCombatActionDefinition* ActionDefinition = SkillComponent->FindActionDefinition(ActiveCombatActionTag);
	AActor* ResolvedTarget = ResolveCombatTarget();
	if (!ActionDefinition || !SkillComponent->CanUseAction(ActiveCombatActionTag))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (ActionDefinition->bRequiresTarget && !ResolvedTarget)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (ResolvedTarget && ActionDefinition->bRotateToTarget)
	{
		FVector ToTarget = ResolvedTarget->GetActorLocation() - AvatarActor->GetActorLocation();
		ToTarget.Z = 0.f;
		if (!ToTarget.IsNearlyZero())
		{
			AvatarActor->SetActorRotation(ToTarget.Rotation());
		}
	}

	UAnimMontage* ActionMontage = nullptr;
	if (!ActionDefinition->Montage.IsNull())
	{
		ActionMontage = ActionDefinition->Montage.LoadSynchronous();
		if (!ActionMontage)
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
	}
	else if (ActionDefinition->HitExecutionType == ETDCombatHitExecutionType::NotifyTrace)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (StaminaCost > 0.f && !CombatComponent->ConsumeStamina(StaminaCost))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AddAbilityStateTag(ActiveStateTag, bHasAddedActiveStateTag);
	SkillComponent->BeginActionExecution(this, ActiveCombatActionTag, ResolvedTarget, ActionDefinition->HitExecutionType);

	bool bExecuted = false;
	if (ActionMontage)
	{
		UAnimInstance* AnimInstance = GetAvatarAnimInstance(AvatarActor);
		if (!AnimInstance)
		{
			ClearAbilityState();
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		const float MontageDuration = AnimInstance->Montage_Play(ActionMontage, FMath::Max(ActionDefinition->PlayRate, 0.1f));
		if (MontageDuration <= 0.f)
		{
			ClearAbilityState();
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		if (!ActionDefinition->MontageSection.IsNone())
		{
			AnimInstance->Montage_JumpToSection(ActionDefinition->MontageSection, ActionMontage);
		}

		FOnMontageEnded MontageEndedDelegate;
		MontageEndedDelegate.BindUObject(this, &ThisClass::HandleActionMontageEnded);
		AnimInstance->Montage_SetEndDelegate(MontageEndedDelegate, ActionMontage);

		ActiveAnimInstance = AnimInstance;
		ActiveActionMontage = ActionMontage;
		bExecuted = true;
	}

	if (ActionDefinition->HitExecutionType == ETDCombatHitExecutionType::DirectDamage && ResolvedTarget)
	{
		FTDDamageSpec DamageSpec;
		if (SkillComponent->BuildCurrentActionDamageSpec(DamageSpec))
		{
			UTDCombatLibrary::TryApplyDamage(ResolvedTarget, DamageSpec);
		}
		bExecuted = true;
	}

	if (!bExecuted)
	{
		ClearAbilityState();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (ComboStep > 0)
	{
		SkillComponent->NotifyPrimaryActionActivated(ComboStep, ActiveCombatActionTag);
	}

	if (!CooldownTags.IsEmpty())
	{
		CommitAbilityCooldown(Handle, ActorInfo, ActivationInfo, true);
	}

	if (!ActionMontage)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
}

void UTDCombatActionAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	ClearAbilityState();
	ActiveCombatActionTag = FGameplayTag();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

const FGameplayTagContainer* UTDCombatActionAbility::GetCooldownTags() const
{
	if (!CooldownTags.IsEmpty())
	{
		return &CooldownTags;
	}

	return Super::GetCooldownTags();
}

void UTDCombatActionAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	if (CooldownTags.IsEmpty())
	{
		return;
	}

	const float ResolvedCooldownDuration = ResolveCooldownDuration();
	if (ResolvedCooldownDuration <= 0.f)
	{
		return;
	}

	const FGameplayEffectSpecHandle CooldownSpecHandle = MakeOutgoingGameplayEffectSpec(Handle, ActorInfo, ActivationInfo,
		UTDActionCooldownEffect::StaticClass(), GetAbilityLevel(Handle, ActorInfo));
	if (!CooldownSpecHandle.IsValid())
	{
		return;
	}

	CooldownSpecHandle.Data->DynamicGrantedTags.AppendTags(CooldownTags);
	CooldownSpecHandle.Data->SetSetByCallerMagnitude(TDGameplayTags::Data_Cooldown_Duration, ResolvedCooldownDuration);
	ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, CooldownSpecHandle);
}

UTDCombatComponent* UTDCombatActionAbility::GetCombatComponent() const
{
	return Cast<UTDCombatComponent>(GetAbilitySystemComponentFromActorInfo());
}

UTDSkillComponent* UTDCombatActionAbility::GetSkillComponent() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	return AvatarActor ? AvatarActor->FindComponentByClass<UTDSkillComponent>() : nullptr;
}

FGameplayTag UTDCombatActionAbility::ResolveRequestedCombatActionTag(int32& OutComboStep) const
{
	OutComboStep = 0;

	if (bUsePrimaryComboResolution || PrimaryComboStep > 0)
	{
		if (const UTDSkillComponent* SkillComponent = GetSkillComponent())
		{
			return SkillComponent->ResolvePrimaryActionTag(OutComboStep);
		}
	}

	return CombatActionTag;
}

AActor* UTDCombatActionAbility::ResolveCombatTarget() const
{
	if (!bUseCurrentTarget)
	{
		return nullptr;
	}

	const UTDSkillComponent* SkillComponent = GetSkillComponent();
	return SkillComponent ? SkillComponent->GetCombatTarget() : nullptr;
}

float UTDCombatActionAbility::ResolveCooldownDuration() const
{
	return FMath::Max(CooldownDuration, 0.f);
}

void UTDCombatActionAbility::AddAbilityStateTag(const FGameplayTag& Tag, bool& bTagAdded)
{
	if (bTagAdded || !Tag.IsValid())
	{
		return;
	}

	UTDCombatComponent* CombatComponent = GetCombatComponent();
	if (!CombatComponent)
	{
		return;
	}

	CombatComponent->AddLooseGameplayTag(Tag);
	bTagAdded = true;
}

void UTDCombatActionAbility::RemoveAbilityStateTag(const FGameplayTag& Tag, bool& bTagAdded)
{
	if (!bTagAdded || !Tag.IsValid())
	{
		return;
	}

	if (UTDCombatComponent* CombatComponent = GetCombatComponent())
	{
		CombatComponent->RemoveLooseGameplayTag(Tag);
	}
	bTagAdded = false;
}

void UTDCombatActionAbility::HandleActionMontageEnded(UAnimMontage* Montage, const bool bInterrupted)
{
	if (Montage != ActiveActionMontage || !IsActive())
	{
		return;
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bInterrupted);
}

void UTDCombatActionAbility::ClearAbilityState()
{
	if (UTDSkillComponent* SkillComponent = GetSkillComponent())
	{
		SkillComponent->EndActionExecution(this);
	}

	UAnimInstance* AnimInstance = ActiveAnimInstance.Get();
	if (AnimInstance && ActiveActionMontage)
	{
		FOnMontageEnded EmptyMontageEndedDelegate;
		AnimInstance->Montage_SetEndDelegate(EmptyMontageEndedDelegate, ActiveActionMontage);
		if (AnimInstance->Montage_IsPlaying(ActiveActionMontage))
		{
			AnimInstance->Montage_Stop(0.1f, ActiveActionMontage);
		}
	}

	ActiveAnimInstance.Reset();
	ActiveActionMontage = nullptr;
	RemoveAbilityStateTag(ActiveStateTag, bHasAddedActiveStateTag);
}

UTDPlayerPrimaryAttackAbility::UTDPlayerPrimaryAttackAbility()
{
	SetAssetTags(FGameplayTagContainer(TDGameplayTags::Action_Attack_Primary));
	CombatActionTag = TDGameplayTags::Action_Attack_Primary;
	ActiveStateTag = TDGameplayTags::State_Attacking;
	bUsePrimaryComboResolution = true;

	ActivationBlockedTags.AddTag(TDGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Rolling);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Skill);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Stunned);
}

UTDPlayerPrimaryAttack01Ability::UTDPlayerPrimaryAttack01Ability()
{
	SetAssetTags(FGameplayTagContainer(TDGameplayTags::Action_Attack_Primary_01));
	CombatActionTag = TDGameplayTags::Action_Attack_Primary_01;
	ActiveStateTag = TDGameplayTags::State_Attacking;
	PrimaryComboStep = 1;

	ActivationBlockedTags.AddTag(TDGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Rolling);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Skill);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Stunned);
}

UTDPlayerPrimaryAttack02Ability::UTDPlayerPrimaryAttack02Ability()
{
	SetAssetTags(FGameplayTagContainer(TDGameplayTags::Action_Attack_Primary_02));
	CombatActionTag = TDGameplayTags::Action_Attack_Primary_02;
	ActiveStateTag = TDGameplayTags::State_Attacking;
	PrimaryComboStep = 2;

	ActivationBlockedTags.AddTag(TDGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Rolling);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Skill);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Stunned);
}

UTDPlayerPrimaryAttack03Ability::UTDPlayerPrimaryAttack03Ability()
{
	SetAssetTags(FGameplayTagContainer(TDGameplayTags::Action_Attack_Primary_03));
	CombatActionTag = TDGameplayTags::Action_Attack_Primary_03;
	ActiveStateTag = TDGameplayTags::State_Attacking;
	PrimaryComboStep = 3;

	ActivationBlockedTags.AddTag(TDGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Rolling);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Skill);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Stunned);
}

UTDPlayerSkillQAbility::UTDPlayerSkillQAbility()
{
	SetAssetTags(FGameplayTagContainer(TDGameplayTags::Action_Skill_Q));
	CombatActionTag = TDGameplayTags::Action_Skill_Q;
	ActiveStateTag = TDGameplayTags::State_Skill;
	StaminaCost = 20.f;
	CooldownTags.AddTag(TDGameplayTags::Cooldown_Skill_Q);
	CooldownDuration = 0.6f;

	ActivationBlockedTags.AddTag(TDGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Attacking);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Rolling);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Skill);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Stunned);
}

UTDPlayerSkillEAbility::UTDPlayerSkillEAbility()
{
	SetAssetTags(FGameplayTagContainer(TDGameplayTags::Action_Skill_E));
	CombatActionTag = TDGameplayTags::Action_Skill_E;
	ActiveStateTag = TDGameplayTags::State_Skill;
	StaminaCost = 35.f;
	CooldownTags.AddTag(TDGameplayTags::Cooldown_Skill_E);
	CooldownDuration = 0.75f;

	ActivationBlockedTags.AddTag(TDGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Attacking);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Rolling);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Skill);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Stunned);
}

UTDMonsterPrimaryAttackAbility::UTDMonsterPrimaryAttackAbility()
{
	SetAssetTags(FGameplayTagContainer(TDGameplayTags::Action_Monster_Attack_Primary));
	CombatActionTag = TDGameplayTags::Action_Monster_Attack_Primary;
	ActiveStateTag = TDGameplayTags::State_Attacking;
	CooldownTags.AddTag(TDGameplayTags::Cooldown_Monster_Attack_Primary);
	CooldownDuration = 0.5f;

	ActivationBlockedTags.AddTag(TDGameplayTags::State_Attacking);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Skill);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Stunned);
}

UTDMonsterSkill01Ability::UTDMonsterSkill01Ability()
{
	SetAssetTags(FGameplayTagContainer(TDGameplayTags::Action_Monster_Skill_01));
	CombatActionTag = TDGameplayTags::Action_Monster_Skill_01;
	ActiveStateTag = TDGameplayTags::State_Skill;
	CooldownTags.AddTag(TDGameplayTags::Cooldown_Monster_Skill_01);
	CooldownDuration = 0.75f;

	ActivationBlockedTags.AddTag(TDGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Attacking);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Skill);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Stunned);
}
