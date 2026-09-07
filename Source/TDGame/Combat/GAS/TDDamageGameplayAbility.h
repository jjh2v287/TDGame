#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "TDDamageGameplayAbility.generated.h"

UCLASS(NotBlueprintable)
class TDGAME_API UTDDamageGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UTDDamageGameplayAbility();
	bool DidLastCastSucceed() const;
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual bool CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

private:
	mutable FActiveGameplayEffectHandle CastCooldownHandle;
	bool bLastCastSucceeded = false;
};
