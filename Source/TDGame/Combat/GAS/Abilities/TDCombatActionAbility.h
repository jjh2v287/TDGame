#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "TDCombatActionAbility.generated.h"

class UAnimInstance;
class UAnimMontage;
class UTDCombatComponent;
class UTDSkillComponent;

UCLASS(Abstract)
class TDGAME_API UTDCombatActionAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UTDCombatActionAbility();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual const FGameplayTagContainer* GetCooldownTags() const override;
	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	UPROPERTY(EditDefaultsOnly, Category="Combat")
	FGameplayTag CombatActionTag;

	UPROPERTY(EditDefaultsOnly, Category="Combat")
	FGameplayTag ActiveStateTag;

	UPROPERTY(EditDefaultsOnly, Category="Combat", meta=(ClampMin="0.0"))
	float StaminaCost = 0.f;

	UPROPERTY(EditDefaultsOnly, Category="Combat")
	bool bUsePrimaryComboResolution = false;

	UPROPERTY(EditDefaultsOnly, Category="Combat", meta=(ClampMin="0"))
	int32 PrimaryComboStep = 0;

	UPROPERTY(EditDefaultsOnly, Category="Combat")
	bool bUseCurrentTarget = true;

	UPROPERTY(EditDefaultsOnly, Category="Cooldown")
	FGameplayTagContainer CooldownTags;

	UPROPERTY(EditDefaultsOnly, Category="Cooldown", meta=(ClampMin="0.0"))
	float CooldownDuration = 0.f;

	UTDCombatComponent* GetCombatComponent() const;
	UTDSkillComponent* GetSkillComponent() const;

	virtual FGameplayTag ResolveRequestedCombatActionTag(int32& OutComboStep) const;
	virtual AActor* ResolveCombatTarget() const;
	virtual float ResolveCooldownDuration() const;

private:
	void AddAbilityStateTag(const FGameplayTag& Tag, bool& bTagAdded);
	void RemoveAbilityStateTag(const FGameplayTag& Tag, bool& bTagAdded);
	void HandleActionMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	void ClearAbilityState();

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveActionMontage = nullptr;

	TWeakObjectPtr<UAnimInstance> ActiveAnimInstance;
	FGameplayTag ActiveCombatActionTag;
	bool bHasAddedActiveStateTag = false;
};

UCLASS()
class TDGAME_API UTDPlayerPrimaryAttackAbility : public UTDCombatActionAbility
{
	GENERATED_BODY()

public:
	UTDPlayerPrimaryAttackAbility();
};

UCLASS()
class TDGAME_API UTDPlayerPrimaryAttack01Ability : public UTDCombatActionAbility
{
	GENERATED_BODY()

public:
	UTDPlayerPrimaryAttack01Ability();
};

UCLASS()
class TDGAME_API UTDPlayerPrimaryAttack02Ability : public UTDCombatActionAbility
{
	GENERATED_BODY()

public:
	UTDPlayerPrimaryAttack02Ability();
};

UCLASS()
class TDGAME_API UTDPlayerPrimaryAttack03Ability : public UTDCombatActionAbility
{
	GENERATED_BODY()

public:
	UTDPlayerPrimaryAttack03Ability();
};

UCLASS()
class TDGAME_API UTDPlayerSkillQAbility : public UTDCombatActionAbility
{
	GENERATED_BODY()

public:
	UTDPlayerSkillQAbility();
};

UCLASS()
class TDGAME_API UTDPlayerSkillEAbility : public UTDCombatActionAbility
{
	GENERATED_BODY()

public:
	UTDPlayerSkillEAbility();
};

UCLASS()
class TDGAME_API UTDMonsterPrimaryAttackAbility : public UTDCombatActionAbility
{
	GENERATED_BODY()

public:
	UTDMonsterPrimaryAttackAbility();
};

UCLASS()
class TDGAME_API UTDMonsterSkill01Ability : public UTDCombatActionAbility
{
	GENERATED_BODY()

public:
	UTDMonsterSkill01Ability();
};
