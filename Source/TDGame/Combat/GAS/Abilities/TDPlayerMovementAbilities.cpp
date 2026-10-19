#include "Combat/GAS/Abilities/TDPlayerMovementAbilities.h"

#include "Characters/TDGameCharacter.h"
#include "Combat/Skills/TDSkillComponent.h"
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
	const UTDCombatComponent* CombatComponent = PlayerCharacter ? PlayerCharacter->GetCombatComponent() : nullptr;
	if (!CombatComponent || CombatComponent->IsFrozen() || !PlayerCharacter->CanPlayRollAnimation())
	{
		return false;
	}

	if (PlayerCharacter->IsRollBlockedByCurrentAction())
	{
		return false;
	}

	return PlayerCharacter->CanStartJump();
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
	bHasAddedRollingTag = true;
	const bool bIsAttackCancelRoll = CombatComponent->HasMatchingGameplayTag(TDGameplayTags::State_Attacking);

	const float RollDuration = PlayerCharacter->PlayRollAnimation(PlayerCharacter->ConsumePendingRollDirection());
	if (RollDuration <= 0.f)
	{
		ClearRollState();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (bIsAttackCancelRoll)
	{
		if (UTDSkillComponent* SkillComponent = PlayerCharacter->GetSkillComponent())
		{
			SkillComponent->CancelCurrentAttackAction();
		}
		PlayerCharacter->NotifyAttackCancelRollStarted();
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	RollStartTimeSeconds = World->GetTimeSeconds();
	World->GetTimerManager().SetTimer(FinishAbilityTimerHandle, this, &ThisClass::HandleRollFinished, RollDuration, false);
	RefreshRollInvulnerability();
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

void UTDPlayerRollAbility::RefreshRollInvulnerability()
{
	UWorld* World = GetWorld();
	if (!World || !bHasAddedRollingTag)
	{
		return;
	}

	const double ElapsedSeconds = World->GetTimeSeconds() - RollStartTimeSeconds;
	SetRollInvulnerable(TDPlayerRoll::IsInvulnerableAtElapsed(ElapsedSeconds));
	if (ElapsedSeconds >= TDPlayerRoll::InvulnerabilityEndSeconds)
	{
		return;
	}

	const double NextBoundarySeconds = ElapsedSeconds < TDPlayerRoll::InvulnerabilityStartSeconds ? TDPlayerRoll::InvulnerabilityStartSeconds : TDPlayerRoll::InvulnerabilityEndSeconds;
	const float DelaySeconds = static_cast<float>(FMath::Max(NextBoundarySeconds - ElapsedSeconds, static_cast<double>(UE_KINDA_SMALL_NUMBER)));
	World->GetTimerManager().SetTimer(InvulnerabilityTimerHandle, this, &ThisClass::RefreshRollInvulnerability, DelaySeconds, false);
}

void UTDPlayerRollAbility::SetRollInvulnerable(const bool bIsInvulnerable)
{
	UTDCombatComponent* CombatComponent = GetCombatComponent();
	if (!CombatComponent || bHasAddedInvulnerableTag == bIsInvulnerable)
	{
		return;
	}

	bHasAddedInvulnerableTag = bIsInvulnerable;
	if (bIsInvulnerable)
	{
		CombatComponent->AddLooseGameplayTag(TDGameplayTags::State_Invulnerable);
		return;
	}
	CombatComponent->RemoveLooseGameplayTag(TDGameplayTags::State_Invulnerable);
}

void UTDPlayerRollAbility::ClearRollState()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FinishAbilityTimerHandle);
		World->GetTimerManager().ClearTimer(InvulnerabilityTimerHandle);
	}

	if (ATDGameCharacter* PlayerCharacter = GetPlayerCharacter())
	{
		PlayerCharacter->EndRollAnimation();
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
	return PlayerCharacter && PlayerCharacter->CanStartJump();
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
