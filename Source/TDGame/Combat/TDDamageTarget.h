#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Actor.h"
#include "Combat/TDDamageTypes.h"
#include "TDDamageTarget.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTDCombatComponent;

UCLASS()
class TDGAME_API ATDDamageTarget : public AActor, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ATDDamageTarget();
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void Tick(float DeltaSeconds) override;
	UTDCombatComponent* GetCombatComponent() const { return CombatComponent; }

protected:
	virtual void BeginPlay() override;

private:
	void UpdateHealthLabel();
	void HandleDamage(const FTDDamageResult& Result, const FTDDamageContext& Context);
	void HandleDeath(const FTDDamageContext& Context);
	void HandleFreezeChanged(bool bIsFrozen);

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UCapsuleComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UTextRenderComponent> HealthLabel;

	UPROPERTY(VisibleAnywhere, Category="Combat")
	TObjectPtr<UTDCombatComponent> CombatComponent;
};
