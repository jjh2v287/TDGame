#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "TDCombatAttributeSet.generated.h"

#define TD_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS()
class TDGAME_API UTDCombatAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category="Combat|Health")
	FGameplayAttributeData Health = 100.f;
	TD_ATTRIBUTE_ACCESSORS(UTDCombatAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category="Combat|Health")
	FGameplayAttributeData MaxHealth = 100.f;
	TD_ATTRIBUTE_ACCESSORS(UTDCombatAttributeSet, MaxHealth)

	UPROPERTY(BlueprintReadOnly, Category="Combat|Stamina")
	FGameplayAttributeData Stamina = 100.f;
	TD_ATTRIBUTE_ACCESSORS(UTDCombatAttributeSet, Stamina)

	UPROPERTY(BlueprintReadOnly, Category="Combat|Stamina")
	FGameplayAttributeData MaxStamina = 100.f;
	TD_ATTRIBUTE_ACCESSORS(UTDCombatAttributeSet, MaxStamina)

	UPROPERTY(BlueprintReadOnly, Category="Combat|Stats")
	FGameplayAttributeData Level = 1.f;
	TD_ATTRIBUTE_ACCESSORS(UTDCombatAttributeSet, Level)

	UPROPERTY(BlueprintReadOnly, Category="Combat|Stats")
	FGameplayAttributeData AttackPower = 10.f;
	TD_ATTRIBUTE_ACCESSORS(UTDCombatAttributeSet, AttackPower)

	UPROPERTY(BlueprintReadOnly, Category="Combat|Stats")
	FGameplayAttributeData SpellPower = 20.f;
	TD_ATTRIBUTE_ACCESSORS(UTDCombatAttributeSet, SpellPower)

	UPROPERTY(BlueprintReadOnly, Category="Combat|Stats")
	FGameplayAttributeData Armor = 0.f;
	TD_ATTRIBUTE_ACCESSORS(UTDCombatAttributeSet, Armor)

	UPROPERTY(BlueprintReadOnly, Category="Combat|Stats")
	FGameplayAttributeData MagicResistance = 0.f;
	TD_ATTRIBUTE_ACCESSORS(UTDCombatAttributeSet, MagicResistance)

	UPROPERTY(BlueprintReadOnly, Category="Combat|Stats")
	FGameplayAttributeData CriticalChance = 0.f;
	TD_ATTRIBUTE_ACCESSORS(UTDCombatAttributeSet, CriticalChance)

	UPROPERTY(BlueprintReadOnly, Category="Combat|Stats")
	FGameplayAttributeData CriticalMultiplier = 1.5f;
	TD_ATTRIBUTE_ACCESSORS(UTDCombatAttributeSet, CriticalMultiplier)

	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& EffectCallback) override;

private:
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;
	void ClampHealthToMaximum();
	void ClampStaminaToMaximum();
};
