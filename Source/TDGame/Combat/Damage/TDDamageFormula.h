#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

namespace TDDamageFormula
{
	TDGAME_API bool RollCritical(bool bCanCrit, float CriticalChance, FRandomStream* RandomStream);
	TDGAME_API float Compute(double RawDamage, bool bIsCritical, double CriticalMultiplier, double Resistance);
}
