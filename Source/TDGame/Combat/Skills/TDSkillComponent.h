#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Combat/Skills/TDCombatActionTypes.h"
#include "TDSkillComponent.generated.h"

class UGameplayAbility;
class UTDCombatStyleDefinition;
struct FTDDamageSpec;

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class TDGAME_API UTDSkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTDSkillComponent();

	UFUNCTION(BlueprintPure, Category="Skill")
	UTDCombatStyleDefinition* GetSkillSet() const { return SkillSet; }

	UFUNCTION(BlueprintCallable, Category="Skill")
	void SetCombatTarget(AActor* NewTarget);

	UFUNCTION(BlueprintPure, Category="Skill")
	AActor* GetCombatTarget() const { return CombatTarget.Get(); }

	UFUNCTION(BlueprintPure, Category="Skill")
	bool HasActionDefinition(FGameplayTag ActionTag) const;

	UFUNCTION(BlueprintPure, Category="Skill")
	FGameplayTag ResolvePrimaryActionTag(int32& OutComboStep) const;

	UFUNCTION(BlueprintCallable, Category="Skill")
	bool CanUseAction(FGameplayTag ActionTag) const;

	UFUNCTION(BlueprintCallable, Category="Skill")
	bool TryPlayReaction(FGameplayTag ReactionTag);

	UFUNCTION(BlueprintPure, Category="Skill")
	float GetActionRange(FGameplayTag ActionTag) const;

	UFUNCTION(BlueprintPure, Category="Skill")
	FGameplayTag GetCurrentActionTag() const { return CurrentActionTag; }

	UFUNCTION(BlueprintPure, Category="Skill")
	AActor* GetCurrentActionTarget() const { return CurrentActionTarget.Get(); }

	bool HasCurrentActionContext() const { return CurrentActionTag.IsValid(); }
	bool ShouldUseTraceForCurrentAction() const;
	bool BuildCurrentActionDamageSpec(FTDDamageSpec& OutDamageSpec) const;

	const FTDCombatActionDefinition* FindActionDefinition(FGameplayTag ActionTag) const;
	const FTDCombatReactionDefinition* FindReactionDefinition(FGameplayTag ReactionTag) const;
	void BeginActionExecution(UGameplayAbility* OwningAbility, FGameplayTag ActionTag, AActor* TargetActor, ETDCombatHitExecutionType HitExecutionType);
	void EndActionExecution(UGameplayAbility* OwningAbility);
	void NotifyPrimaryActionActivated(int32 ComboStep, FGameplayTag ResolvedActionTag);
	void ResetPrimaryCombo();
	bool BufferCombatInput(FGameplayTag RequestedActionTag, AActor* TargetActor);
	void ClearBufferedInput();
	void BeginInputBufferWindow();
	void EndInputBufferWindow();
	void ClearCurrentActionContext();

protected:
	virtual void BeginPlay() override;

private:
	bool BuildDamageSpecForAction(const FTDCombatActionDefinition& ActionDefinition, FTDDamageSpec& OutDamageSpec) const;
	void SetCurrentActionContext(FGameplayTag ActionTag, AActor* TargetActor, ETDCombatHitExecutionType HitExecutionType);
	void HandleComboWindowChanged(FGameplayTag Tag, int32 NewCount);
	void HandleAttackStateChanged(FGameplayTag Tag, int32 NewCount);
	void HandleRecoveryStateChanged(FGameplayTag Tag, int32 NewCount);
	void HandleSkillStateChanged(FGameplayTag Tag, int32 NewCount);
	void HandleDeathStateChanged(FGameplayTag Tag, int32 NewCount);
	void HandleDamageReceived(const FTDDamageResult& Result, const FTDDamageContext& Context);
	void HandleOwnerDeath(const FTDDamageContext& Context);
	bool TryConsumeBufferedInput();
	bool HasBufferedInput() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skill", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UTDCombatStyleDefinition> SkillSet = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combo", meta=(AllowPrivateAccess="true", ClampMin="1"))
	int32 MaxPrimaryComboCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combo", meta=(AllowPrivateAccess="true", ClampMin="0.1"))
	float PrimaryComboResetWindow = 0.85f;

	TWeakObjectPtr<AActor> CombatTarget;
	TWeakObjectPtr<AActor> CurrentActionTarget;
	TWeakObjectPtr<UGameplayAbility> CurrentActionOwningAbility;
	FGameplayTag CurrentActionTag;
	ETDCombatHitExecutionType CurrentHitExecutionType = ETDCombatHitExecutionType::NotifyTrace;
	double LastPrimaryAttackTimestamp = -1.0;
	int32 CurrentPrimaryComboStep = 0;
	bool bIsComboWindowOpen = false;
	int32 ActiveInputBufferWindowCount = 0;
	FGameplayTag BufferedActionTag;
	TWeakObjectPtr<AActor> BufferedActionTarget;
};
