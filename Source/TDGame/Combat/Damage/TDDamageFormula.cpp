#include "Combat/Damage/TDDamageFormula.h"

bool TDDamageFormula::RollCritical(bool bCanCrit, float CriticalChance, FRandomStream* RandomStream)
{
	if (!bCanCrit || CriticalChance <= 0.f)
	{
		return false;
	}
	if (CriticalChance >= 1.f)
	{
		return true;
	}
	if (!RandomStream)
	{
		return false;
	}
	return RandomStream->FRand() < CriticalChance;
}

float TDDamageFormula::Compute(double RawDamage, bool bIsCritical, double CriticalMultiplier, double Resistance)
{
	const double Multiplier = bIsCritical ? FMath::Max(1., CriticalMultiplier) : 1.;
	return static_cast<float>(FMath::Min(static_cast<double>(TNumericLimits<float>::Max()), RawDamage * Multiplier * 100. / (100. + Resistance)));
}
