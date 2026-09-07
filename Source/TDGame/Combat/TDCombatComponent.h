#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Combat/TDDamageTypes.h"
#include "TDCombatComponent.generated.h"

class UBrainComponent;
class UMovementComponent;
class AController;
class UTDCombatAttributeSet;

DECLARE_MULTICAST_DELEGATE_TwoParams(FTDOnCombatDamage, const FTDDamageResult&, const FTDDamageContext&);
DECLARE_MULTICAST_DELEGATE_OneParam(FTDOnCombatDeath, const FTDDamageContext&);
DECLARE_MULTICAST_DELEGATE_OneParam(FTDOnCombatFreezeChanged, bool);

USTRUCT()
struct FTDStatusRuntime
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UTDStatusDefinition> Definition;

	UPROPERTY()
	FTDDamageContext Context;

	uint64 Id = 0;
	double ExpireTime = 0.;
	double NextPulseTime = 0.;
	double LastBuildupTime = 0.;
	float Buildup = 0.f;
	bool bIsActive = false;
	bool bFreezesTarget = false;
	bool bIsApplyingEffect = false;
	FActiveGameplayEffectHandle EffectHandle;
};

struct FTDPendingCombatDamage
{
	FGameplayEffectContextHandle EffectContext;
	FTDDamageContext CombatContext;
	FTDDamageResult Result;
};

USTRUCT()
struct FTDFrozenMovement
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UMovementComponent> Component;

	bool bWasTickEnabled = false;
	uint8 MovementMode = 0;
	uint8 CustomMovementMode = 0;
};

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class TDGAME_API UTDCombatComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UTDCombatComponent();

	UFUNCTION(BlueprintPure, Category="Combat")
	FTDCombatStats GetStats() const;

	UFUNCTION(BlueprintCallable, Category="Combat")
	void SetStats(const FTDCombatStats& NewStats, bool bResetHealth = true);

	UFUNCTION(BlueprintCallable, Category="Combat")
	void SetCombatLevel(int32 NewLevel);

	UFUNCTION(BlueprintPure, Category="Combat")
	float GetCurrentHealth() const;

	UFUNCTION(BlueprintPure, Category="Combat")
	bool IsAlive() const;

	UFUNCTION(BlueprintPure, Category="Combat")
	bool IsFrozen() const;

	bool TryCastDamageDefinition(UTDDamageDefinition* Definition, const FVector& Target);

	FTDDamageResult ReceiveDamage(float RawDamage, ETDDamageElement Element, bool bCanCrit, const FTDDamageContext& Context);
	void ApplyStatus(UTDStatusDefinition* Definition, const FTDDamageContext& Context, float BuildupAmount = 0.f);

	FTDOnCombatDamage OnDamaged;
	FTDOnCombatDeath OnDeath;
	FTDOnCombatFreezeChanged OnFreezeChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void UpdateStatuses();
	void ScheduleStatusUpdate();
	void ClearStatuses();
	void UpdateFreezeState();
	void SetFrozen(bool bShouldFreeze);
	void ExecuteStatusEvent(const FTDStatusRuntime& Status, ETDDamageEvent Event);
	void HandleStatusEffectRemoved(const FGameplayEffectRemovalInfo& Info, uint64 StatusId);
	void HandleFrozenTagChanged(const FGameplayTag Tag, int32 NewCount);
	void HandleHealthChanged(const FOnAttributeChangeData& Change);
	void ApplyInitialAttributes(bool bResetHealth);
	void RefreshDeathState();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat", meta=(AllowPrivateAccess="true"))
	FTDCombatStats Stats;

	UPROPERTY(Transient)
	TObjectPtr<UTDCombatAttributeSet> CombatAttributes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTDDamageDefinition>> GrantedDamageDefinitions;

	UPROPERTY(Transient)
	TArray<FTDStatusRuntime> Statuses;

	UPROPERTY(Transient)
	TArray<FTDFrozenMovement> FrozenMovements;

	UPROPERTY(Transient)
	TWeakObjectPtr<UBrainComponent> PausedBrain;

	UPROPERTY(Transient)
	TWeakObjectPtr<AController> FrozenController;

	FTimerHandle StatusTimer;
	FDelegateHandle HealthChangedHandle;
	FDelegateHandle FrozenTagChangedHandle;
	FActiveGameplayEffectHandle DeadEffectHandle;
	TArray<FTDPendingCombatDamage> PendingDamage;
	uint64 NextStatusId = 1;
	uint64 StatusGeneration = 0;
	float SavedCustomTimeDilation = 1.f;
	bool bWasActorTickEnabled = false;
	bool bIsFrozen = false;
	bool bIsEndingPlay = false;
	bool bDeathReported = false;
	bool bIsUpdatingStatuses = false;
	bool bIsChangingFreeze = false;
	bool bIsApplyingInitialStats = false;
	bool bIsApplyingDeadEffect = false;
};
