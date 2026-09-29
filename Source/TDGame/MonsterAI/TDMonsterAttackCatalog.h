#pragma once

#include "CoreMinimal.h"

class UTDDamageDefinition;

namespace TDMonsterAttackCatalog
{
	TDGAME_API void CreateAttacks(UObject* Outer, TMap<FName, UTDDamageDefinition*>& OutAttacks);
}
