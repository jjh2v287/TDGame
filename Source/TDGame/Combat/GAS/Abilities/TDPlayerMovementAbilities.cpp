#include "Combat/GAS/Abilities/TDPlayerMovementAbilities.h"

#include "Characters/TDGameCharacter.h"
#include "Combat/TDCombatComponent.h"
#include "Core/TDGameplayTags.h"
#include "Engine/World.h"
#include "TimerManager.h"

UTDPlayerRollAbility::UTDPlayerRollAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(TDGameplayTags::Action_Roll);
	SetAssetTags(AssetTags);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Rolling);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Attacking);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Skill);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Stunned);
}

bool UTDPlayerRollAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const ATDGameCharacter* PlayerCharacter = ActorInfo ? Cast<ATDGameCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!PlayerCharacter)
	{
		return false;
	}

	return !PlayerCharacter->IsRollPlaying() && PlayerCharacter->CanJump();
}

void UTDPlayerRollAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ATDGameCharacter* PlayerCharacter = GetPlayerCharacter();
	UTDCombatComponent* CombatComponent = GetCombatComponent();
	if (!PlayerCharacter || !CombatComponent)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CombatComponent->ConsumeStamina(PlayerCharacter->GetRollStaminaCost()))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	CombatComponent->AddLooseGameplayTag(TDGameplayTags::State_Rolling);
	CombatComponent->AddLooseGameplayTag(TDGameplayTags::State_Invulnerable);
	bHasAddedRollingTag = true;
	bHasAddedInvulnerableTag = true;

	const float RollDuration = PlayerCharacter->PlayRollMontageAbility(PlayerCharacter->ConsumePendingRollDirection());
	if (RollDuration <= 0.f)
	{
		ClearRollState();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(FinishAbilityTimerHandle, this, &ThisClass::HandleRollFinished, RollDuration, false);
	}
}

void UTDPlayerRollAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	ClearRollState();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

ATDGameCharacter* UTDPlayerRollAbility::GetPlayerCharacter() const
{
	return Cast<ATDGameCharacter>(GetAvatarActorFromActorInfo());
}

UTDCombatComponent* UTDPlayerRollAbility::GetCombatComponent() const
{
	return Cast<UTDCombatComponent>(GetAbilitySystemComponentFromActorInfo());
}

void UTDPlayerRollAbility::HandleRollFinished()
{
	K2_EndAbility();
}

void UTDPlayerRollAbility::ClearRollState()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FinishAbilityTimerHandle);
	}

	if (ATDGameCharacter* PlayerCharacter = GetPlayerCharacter())
	{
		PlayerCharacter->EndRollMontageAbility();
	}

	UTDCombatComponent* CombatComponent = GetCombatComponent();
	if (!CombatComponent)
	{
		return;
	}

	if (bHasAddedRollingTag)
	{
		CombatComponent->RemoveLooseGameplayTag(TDGameplayTags::State_Rolling);
		bHasAddedRollingTag = false;
	}

	if (bHasAddedInvulnerableTag)
	{
		CombatComponent->RemoveLooseGameplayTag(TDGameplayTags::State_Invulnerable);
		bHasAddedInvulnerableTag = false;
	}
}

UTDPlayerJumpAbility::UTDPlayerJumpAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerExecution;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(TDGameplayTags::Action_Jump);
	SetAssetTags(AssetTags);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Rolling);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Stunned);
}

bool UTDPlayerJumpAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const ATDGameCharacter* PlayerCharacter = ActorInfo ? Cast<ATDGameCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	return PlayerCharacter && PlayerCharacter->CanJump();
}

void UTDPlayerJumpAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (ATDGameCharacter* PlayerCharacter = Cast<ATDGameCharacter>(GetAvatarActorFromActorInfo()))
	{
		PlayerCharacter->Jump();
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
