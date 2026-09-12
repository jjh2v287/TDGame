#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "TDCombatCharacter.generated.h"

class UGameplayAbility;
class UGameplayEffect;
class UTDCombatAttributeSet;
class UTDCombatComponent;
class UTDDamageDefinition;

UCLASS(Abstract)
class TDGAME_API ATDCombatCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ATDCombatCharacter();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	UTDCombatComponent* GetCombatComponent() const { return CombatComponent.Get(); }
	bool CastDamageSpell(int32 Slot, const FVector& Target);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UTDCombatComponent> CombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UTDCombatAttributeSet> CombatAttributes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
	TArray<TObjectPtr<UTDDamageDefinition>> DamageSpells;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> StartupAbilities;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Abilities")
	TArray<TSubclassOf<UGameplayEffect>> StartupEffects;
};
