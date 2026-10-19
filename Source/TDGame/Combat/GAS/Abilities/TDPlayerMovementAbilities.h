#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/TimerHandle.h"
#include "TDPlayerMovementAbilities.generated.h"

class ATDGameCharacter;
class UTDCombatComponent;

namespace TDPlayerRoll
{
	constexpr double InvulnerabilityStartSeconds = 0.04;
	constexpr double InvulnerabilityEndSeconds = 0.34;
	constexpr double CancelRollLockoutSeconds = 0.9;

	inline bool IsInvulnerableAtElapsed(const double ElapsedSeconds)
	{
		return ElapsedSeconds >= InvulnerabilityStartSeconds && ElapsedSeconds < InvulnerabilityEndSeconds;
	}
}

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
	void RefreshRollInvulnerability();
	void SetRollInvulnerable(bool bIsInvulnerable);

	bool bHasAddedRollingTag = false;
	bool bHasAddedInvulnerableTag = false;
	double RollStartTimeSeconds = 0.0;
	FTimerHandle FinishAbilityTimerHandle;
	FTimerHandle InvulnerabilityTimerHandle;
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
