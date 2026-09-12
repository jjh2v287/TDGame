#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/TimerHandle.h"
#include "TDPlayerMovementAbilities.generated.h"

class ATDGameCharacter;
class UTDCombatComponent;

UCLASS()
class TDGAME_API UTDPlayerRollAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UTDPlayerRollAbility();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	ATDGameCharacter* GetPlayerCharacter() const;
	UTDCombatComponent* GetCombatComponent() const;
	void HandleRollFinished();
	void ClearRollState();

	bool bHasAddedRollingTag = false;
	bool bHasAddedInvulnerableTag = false;
	FTimerHandle FinishAbilityTimerHandle;
};

UCLASS()
class TDGAME_API UTDPlayerJumpAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UTDPlayerJumpAbility();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
