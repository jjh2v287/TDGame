#pragma once

#include "CoreMinimal.h"

class UTDDamageDefinition;

enum class ETDTelegraphStyle : uint8
{
	Danger,
	Unblockable,
	Safe
};

namespace TDMonsterAttackCatalog
{
	TDGAME_API void CreateAttacks(UObject* Outer, TMap<FName, UTDDamageDefinition*>& OutAttacks);
	TDGAME_API void ApplyTelegraph(UTDDamageDefinition& Definition, ETDTelegraphStyle Style);
	TDGAME_API UTDDamageDefinition* CreateWindupSpell(UObject* Outer, FName WrapperName, UTDDamageDefinition* Spell, float WindupSeconds, ETDTelegraphStyle Style);
}
