#include "Combat/GAS/Abilities/TDReactionAbility.h"

#include "Animation/AnimSequence.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Combat/GAS/TDAbilityTask_PlayActionTimeline.h"
#include "Combat/Skills/TDCombatActionTypes.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Core/TDGameplayTags.h"

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

	if (!ReactionDefinition->Action.Animation.LoadSynchronous())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!AvatarActor->FindComponentByClass<UTDCharacterAnimationComponent>())
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

	if (bHoldFinalPose)
	{
		UTDCharacterAnimationComponent* CharacterAnimation = AvatarActor->FindComponentByClass<UTDCharacterAnimationComponent>();
		const FTDActionAnimation& Action = ReactionDefinition->Action;
		const float PoseSeconds = CharacterAnimation->PlayDeath(Action.Animation.Get(), Action.PlayRate, Action.EndSeconds);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, PoseSeconds <= 0.f);
		return;
	}

	AddAbilityStateTag(ActiveStateTag, bHasAddedActiveStateTag);

	UTDAbilityTask_PlayActionTimeline* TimelineTask = UTDAbilityTask_PlayActionTimeline::PlayActionTimeline(
		this, ReactionDefinition->Action, FTDDamageContext());
	TimelineTask->OnCompleted.AddDynamic(this, &ThisClass::HandleReactionTimelineCompleted);
	TimelineTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleReactionTimelineInterrupted);
	TimelineTask->ReadyForActivation();
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

void UTDReactionAbility::HandleReactionTimelineCompleted()
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UTDReactionAbility::HandleReactionTimelineInterrupted()
{
	if (!IsActive())
	{
		return;
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UTDReactionAbility::ClearAbilityState()
{
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
	bHoldFinalPose = true;
}
