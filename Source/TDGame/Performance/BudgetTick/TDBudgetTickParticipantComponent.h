#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TDBudgetTickParticipantComponent.generated.h"

UCLASS(Blueprintable, ClassGroup=(Performance), meta=(BlueprintSpawnableComponent))
class TDGAME_API UTDBudgetTickParticipantComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTDBudgetTickParticipantComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category="Budget Tick")
	void SetRuntimeImportanceBias(float InBias);

	UFUNCTION(BlueprintPure, Category="Budget Tick")
	float GetEffectiveImportance() const;

	bool IsBudgetTickEnabled() const { return bEnableBudgetTick; }
	bool ShouldAlwaysTickWhenNear() const { return bAlwaysTickWhenNear; }
	bool ShouldDisableOwnerTickWhenManaged() const { return bDisableOwnerTickWhileManaged; }
	float GetNearDistance() const { return NearDistance; }
	float GetMinUpdateInterval() const { return FMath::Max(0.f, MinUpdateInterval); }
	float GetMaxUpdateInterval() const { return FMath::Max(GetMinUpdateInterval(), MaxUpdateInterval); }

private:
	void RegisterWithScheduler();
	void UnregisterWithScheduler();
	void ApplyManagedTickState(bool bEnableManagement);

	UPROPERTY(EditAnywhere, Category="Budget Tick")
	bool bEnableBudgetTick = true;

	UPROPERTY(EditAnywhere, Category="Budget Tick")
	bool bAlwaysTickWhenNear = true;

	UPROPERTY(EditAnywhere, Category="Budget Tick")
	bool bDisableOwnerTickWhileManaged = true;

	UPROPERTY(EditAnywhere, Category="Budget Tick", meta=(ClampMin="0.0"))
	float BaseImportance = 1.f;

	UPROPERTY(EditAnywhere, Category="Budget Tick", meta=(ClampMin="0.0"))
	float NearDistance = 1800.f;

	UPROPERTY(EditAnywhere, Category="Budget Tick", meta=(ClampMin="0.0"))
	float MinUpdateInterval = 0.10f;

	UPROPERTY(EditAnywhere, Category="Budget Tick", meta=(ClampMin="0.0"))
	float MaxUpdateInterval = 0.75f;

	UPROPERTY(VisibleInstanceOnly, Category="Budget Tick")
	float RuntimeImportanceBias = 0.f;

	UPROPERTY(Transient)
	bool bIsRegisteredWithScheduler = false;

	UPROPERTY(Transient)
	bool bSavedOwnerTickEnabled = false;

	UPROPERTY(Transient)
	bool bHasSavedOwnerTickState = false;
};
