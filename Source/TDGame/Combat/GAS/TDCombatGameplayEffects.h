#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "TDCombatGameplayEffects.generated.h"

UCLASS()
class TDGAME_API UTDInstantDamageEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UTDInstantDamageEffect();
};

UCLASS()
class TDGAME_API UTDDurationStatusEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UTDDurationStatusEffect();
};

UCLASS()
class TDGAME_API UTDFreezeStatusEffect : public UTDDurationStatusEffect
{
	GENERATED_BODY()

public:
	UTDFreezeStatusEffect();
};

UCLASS()
class TDGAME_API UTDDamageCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UTDDamageCooldownEffect();
};

UCLASS()
class TDGAME_API UTDActionCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UTDActionCooldownEffect();
};

UCLASS()
class TDGAME_API UTDDeadEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UTDDeadEffect();
};
