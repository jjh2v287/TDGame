#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MonsterAI/TDMonsterActionRegistry.h"
#include "TDMonsterBody.generated.h"

class UTDCombatComponent;
class UTDDamageDefinition;
struct FTDMonsterStatValues;

struct FTDMonsterAbilityRequest
{
	FName AbilityName;
	UTDDamageDefinition* Spell = nullptr;
	float Range = 0.f;
	TWeakObjectPtr<AActor> Target;
	FVector TargetLocation = FVector::ZeroVector;
};

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UTDMonsterBody : public UInterface
{
	GENERATED_BODY()
};

class TDGAME_API ITDMonsterBody
{
	GENERATED_BODY()

public:
	virtual FVector GetBodyLocation() const = 0;
	virtual float GetBodyYaw() const = 0;
	virtual float GetBodyRadius() const = 0;
	virtual UTDCombatComponent* GetBodyCombatComponent() const = 0;
	virtual void ApplyBodyStats(const FTDMonsterStatValues& Stats) = 0;
	virtual void ApplyBodyMoveIntent(const FVector2D& Direction, float SpeedScale) = 0;
	virtual void FaceBodyToward(const FVector& Location) = 0;
	virtual float BeginBodyAbility(const FTDMonsterAbilityRequest& Request) = 0;
	virtual void CancelBodyAbility() = 0;
	virtual void BeginBodyStagger(const FVector& SourceLocation, float Seconds) = 0;
	virtual void PresentBody(float DeltaSeconds, ETDMonsterFsmState State) = 0;
};
