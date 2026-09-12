#include "Combat/GAS/TDCombatAttributeSet.h"
#include "GameplayEffectExtension.h"

void UTDCombatAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UTDCombatAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UTDCombatAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);
	if (Attribute == GetMaxHealthAttribute())
	{
		ClampHealthToMaximum();
	}
	if (Attribute == GetMaxStaminaAttribute())
	{
		ClampStaminaToMaximum();
	}
}

void UTDCombatAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& EffectCallback)
{
	Super::PostGameplayEffectExecute(EffectCallback);
	if (EffectCallback.EvaluatedData.Attribute == GetMaxHealthAttribute())
	{
		float ClampedMaxHealth = GetMaxHealth();
		ClampAttribute(GetMaxHealthAttribute(), ClampedMaxHealth);
		if (GetMaxHealth() != ClampedMaxHealth)
		{
			SetMaxHealth(ClampedMaxHealth);
		}
	}

	if (EffectCallback.EvaluatedData.Attribute == GetHealthAttribute()
		|| EffectCallback.EvaluatedData.Attribute == GetMaxHealthAttribute())
	{
		ClampHealthToMaximum();
	}

	if (EffectCallback.EvaluatedData.Attribute == GetStaminaAttribute()
		|| EffectCallback.EvaluatedData.Attribute == GetMaxStaminaAttribute())
	{
		ClampStaminaToMaximum();
	}
}

void UTDCombatAttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (!FMath::IsFinite(NewValue))
	{
		NewValue = 0.f;
	}

	if (Attribute == GetHealthAttribute())
	{
		const float Maximum = FMath::IsFinite(GetMaxHealth()) ? FMath::Max(1.f, GetMaxHealth()) : 1.f;
		NewValue = FMath::Clamp(NewValue, 0.f, Maximum);
		return;
	}

	if (Attribute == GetStaminaAttribute())
	{
		const float Maximum = FMath::IsFinite(GetMaxStamina()) ? FMath::Max(0.f, GetMaxStamina()) : 0.f;
		NewValue = FMath::Clamp(NewValue, 0.f, Maximum);
		return;
	}

	if (Attribute == GetMaxStaminaAttribute())
	{
		NewValue = FMath::Max(0.f, NewValue);
		return;
	}

	if (Attribute == GetMaxHealthAttribute() || Attribute == GetCriticalMultiplierAttribute())
	{
		NewValue = FMath::Max(1.f, NewValue);
		return;
	}

	if (Attribute == GetLevelAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 1.f, 1000.f);
		return;
	}

	if (Attribute == GetCriticalChanceAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, 1.f);
		return;
	}

	if (Attribute == GetAttackPowerAttribute() || Attribute == GetSpellPowerAttribute()
		|| Attribute == GetArmorAttribute() || Attribute == GetMagicResistanceAttribute())
	{
		NewValue = FMath::Max(0.f, NewValue);
	}
}

void UTDCombatAttributeSet::ClampHealthToMaximum()
{
	float ClampedHealth = GetHealth();
	ClampAttribute(GetHealthAttribute(), ClampedHealth);
	if (GetHealth() != ClampedHealth)
	{
		SetHealth(ClampedHealth);
	}
}

void UTDCombatAttributeSet::ClampStaminaToMaximum()
{
	float ClampedStamina = GetStamina();
	ClampAttribute(GetStaminaAttribute(), ClampedStamina);
	if (GetStamina() != ClampedStamina)
	{
		SetStamina(ClampedStamina);
	}
}
