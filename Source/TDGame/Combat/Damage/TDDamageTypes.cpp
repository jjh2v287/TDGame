#include "Combat/Damage/TDDamageTypes.h"

namespace TDDamageValueMath
{
	double GetNonNegativeFinite(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0., static_cast<double>(Value)) : 0.;
	}

	float GetFiniteFloat(double Value)
	{
		return static_cast<float>(FMath::Clamp(Value, 0., static_cast<double>(TNumericLimits<float>::Max())));
	}

	double GetAdditionalLevels(const FTDCombatStats& Stats)
	{
		return FMath::Clamp(Stats.Level, 1, 1000) - 1;
	}
}

float FTDCombatStats::GetMaxHealth() const
{
	return FMath::Max(1.f, TDDamageValueMath::GetFiniteFloat(TDDamageValueMath::GetNonNegativeFinite(BaseMaxHealth) + TDDamageValueMath::GetNonNegativeFinite(HealthPerLevel) * TDDamageValueMath::GetAdditionalLevels(*this)));
}

float FTDCombatStats::GetAttackPower() const
{
	return TDDamageValueMath::GetFiniteFloat(TDDamageValueMath::GetNonNegativeFinite(AttackPower) + TDDamageValueMath::GetNonNegativeFinite(AttackPowerPerLevel) * TDDamageValueMath::GetAdditionalLevels(*this));
}

float FTDCombatStats::GetSpellPower() const
{
	return TDDamageValueMath::GetFiniteFloat(TDDamageValueMath::GetNonNegativeFinite(SpellPower) + TDDamageValueMath::GetNonNegativeFinite(SpellPowerPerLevel) * TDDamageValueMath::GetAdditionalLevels(*this));
}

float FTDScaledValue::Evaluate(const FTDCombatStats& Stats) const
{
	return TDDamageValueMath::GetFiniteFloat(TDDamageValueMath::GetNonNegativeFinite(Base)
		+ TDDamageValueMath::GetNonNegativeFinite(PerLevel) * TDDamageValueMath::GetAdditionalLevels(Stats)
		+ TDDamageValueMath::GetNonNegativeFinite(AttackRatio) * Stats.GetAttackPower()
		+ TDDamageValueMath::GetNonNegativeFinite(SpellRatio) * Stats.GetSpellPower());
}
