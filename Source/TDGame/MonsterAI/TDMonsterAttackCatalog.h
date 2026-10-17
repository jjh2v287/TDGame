#pragma once

#include "CoreMinimal.h"

class UTDDamageDefinition;
struct FSoftObjectPath;

namespace TDMonsterAttackCatalog
{
	TDGAME_API void CreateAttacks(UObject* Outer, TMap<FName, UTDDamageDefinition*>& OutAttacks);
	TDGAME_API TArray<FSoftObjectPath> GetPresentationAssetPaths();
	TDGAME_API void ApplyPresentationAssets(const TMap<FName, UTDDamageDefinition*>& CatalogAttacks);
}
