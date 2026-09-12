#include "Combat/GAS/Abilities/TDReactionAbility.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/Skills/TDCombatActionTypes.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/TDGameplayTags.h"
#include "GameFramework/Character.h"

namespace
{
UAnimInstance* GetReactionAvatarAnimInstance(const AActor* AvatarActor)
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

UTDReactionAbility::UTDReactionAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

bool UTDReactionAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const UTDSkillComponent* SkillComponent = AvatarActor ? AvatarActor->FindComponentByClass<UTDSkillComponent>() : nullptr;
	const UTDCombatComponent* CombatComponent = Cast<UTDCombatComponent>(ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr);
	if (!SkillComponent || !CombatComponent || !ReactionTag.IsValid())
	{
		return false;
	}

	if (!SkillComponent->FindReactionDefinition(ReactionTag))
	{
		return false;
	}

	if (ActiveStateTag.IsValid() && CombatComponent->HasMatchingGameplayTag(ActiveStateTag))
	{
		return false;
	}

	return true;
}

void UTDReactionAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UTDSkillComponent* SkillComponent = GetSkillComponent();
	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!SkillComponent || !AvatarActor || !ReactionTag.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FTDCombatReactionDefinition* ReactionDefinition = SkillComponent->FindReactionDefinition(ReactionTag);
	if (!ReactionDefinition)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAnimMontage* ReactionMontage = ReactionDefinition->Montage.LoadSynchronous();
	if (!ReactionMontage)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAnimInstance* AnimInstance = GetReactionAvatarAnimInstance(AvatarActor);
	if (!AnimInstance)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (bResetComboOnActivate)
	{
		SkillComponent->ClearBufferedInput();
		SkillComponent->ResetPrimaryCombo();
		SkillComponent->ClearCurrentActionContext();
	}

	AddAbilityStateTag(ActiveStateTag, bHasAddedActiveStateTag);

	const float MontageDuration = AnimInstance->Montage_Play(ReactionMontage, FMath::Max(ReactionDefinition->PlayRate, 0.1f));
	if (MontageDuration <= 0.f)
	{
		ClearAbilityState();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!ReactionDefinition->MontageSection.IsNone())
	{
		AnimInstance->Montage_JumpToSection(ReactionDefinition->MontageSection, ReactionMontage);
	}

	FOnMontageEnded MontageEndedDelegate;
	MontageEndedDelegate.BindUObject(this, &ThisClass::HandleReactionMontageEnded);
	AnimInstance->Montage_SetEndDelegate(MontageEndedDelegate, ReactionMontage);

	ActiveAnimInstance = AnimInstance;
	ActiveReactionMontage = ReactionMontage;
}

void UTDReactionAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	ClearAbilityState();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

UTDCombatComponent* UTDReactionAbility::GetCombatComponent() const
{
	return Cast<UTDCombatComponent>(GetAbilitySystemComponentFromActorInfo());
}

UTDSkillComponent* UTDReactionAbility::GetSkillComponent() const
{
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	return AvatarActor ? AvatarActor->FindComponentByClass<UTDSkillComponent>() : nullptr;
}

void UTDReactionAbility::AddAbilityStateTag(const FGameplayTag& Tag, bool& bTagAdded)
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

void UTDReactionAbility::RemoveAbilityStateTag(const FGameplayTag& Tag, bool& bTagAdded)
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

void UTDReactionAbility::HandleReactionMontageEnded(UAnimMontage* Montage, const bool bInterrupted)
{
	if (Montage != ActiveReactionMontage || !IsActive())
	{
		return;
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bInterrupted);
}

void UTDReactionAbility::ClearAbilityState()
{
	UAnimInstance* AnimInstance = ActiveAnimInstance.Get();
	if (AnimInstance && ActiveReactionMontage)
	{
		FOnMontageEnded EmptyMontageEndedDelegate;
		AnimInstance->Montage_SetEndDelegate(EmptyMontageEndedDelegate, ActiveReactionMontage);
		if (AnimInstance->Montage_IsPlaying(ActiveReactionMontage))
		{
			AnimInstance->Montage_Stop(0.1f, ActiveReactionMontage);
		}
	}

	ActiveAnimInstance.Reset();
	ActiveReactionMontage = nullptr;
	RemoveAbilityStateTag(ActiveStateTag, bHasAddedActiveStateTag);
}

UTDReactionHitAbility::UTDReactionHitAbility()
{
	SetAssetTags(FGameplayTagContainer(TDGameplayTags::Action_Reaction_Hit));
	ReactionTag = TDGameplayTags::Action_Reaction_Hit;
	ActiveStateTag = TDGameplayTags::State_Stunned;

	ActivationBlockedTags.AddTag(TDGameplayTags::State_Dead);
}

UTDReactionDeathAbility::UTDReactionDeathAbility()
{
	SetAssetTags(FGameplayTagContainer(TDGameplayTags::Action_Reaction_Death));
	ReactionTag = TDGameplayTags::Action_Reaction_Death;
	bResetComboOnActivate = false;
}
