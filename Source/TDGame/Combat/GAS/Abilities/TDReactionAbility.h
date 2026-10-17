#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Combat/Skills/TDCombatActionTypes.h"
#include "TDReactionAbility.generated.h"

class UTDCombatComponent;
class UTDSkillComponent;
struct FStreamableHandle;

UCLASS(Abstract)
class TDGAME_API UTDReactionAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UTDReactionAbility();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	UPROPERTY(EditDefaultsOnly, Category="Reaction")
	FGameplayTag ReactionTag;

	UPROPERTY(EditDefaultsOnly, Category="Reaction")
	FGameplayTag ActiveStateTag;

	UPROPERTY(EditDefaultsOnly, Category="Reaction")
	bool bResetComboOnActivate = true;

	UPROPERTY(EditDefaultsOnly, Category="Reaction")
	bool bHoldFinalPose = false;

	UTDCombatComponent* GetCombatComponent() const;
	UTDSkillComponent* GetSkillComponent() const;

private:
	void AddAbilityStateTag(const FGameplayTag& Tag, bool& bTagAdded);
	void RemoveAbilityStateTag(const FGameplayTag& Tag, bool& bTagAdded);
	void ClearAbilityState();
	void PlayFinalPoseAndEnd(const FTDActionAnimation& FinalPoseAction);
	void HandleFinalPoseAnimationLoaded();

	UFUNCTION()
	void HandleReactionTimelineCompleted();

	UFUNCTION()
	void HandleReactionTimelineInterrupted();

	UPROPERTY(Transient)
	FTDActionAnimation PendingFinalPoseAction;

	TSharedPtr<FStreamableHandle> FinalPoseLoadHandle;
	bool bHasAddedActiveStateTag = false;
};

UCLASS()
class TDGAME_API UTDReactionHitAbility : public UTDReactionAbility
{
	GENERATED_BODY()

public:
	UTDReactionHitAbility();
};

UCLASS()
class TDGAME_API UTDReactionDeathAbility : public UTDReactionAbility
{
	GENERATED_BODY()

public:
	UTDReactionDeathAbility();
};
