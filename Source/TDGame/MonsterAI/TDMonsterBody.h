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
	float WindupSeconds = 0.f;
	bool bIsDanger = false;
};

struct FTDMonsterSummonRequest
{
	FName SpeciesName;
	int32 Count = 3;
	float RadiusCm = 600.f;
	float Seconds = 1.f;
	TWeakObjectPtr<AActor> Target;
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
	virtual float BeginBodyFlight(float AltitudeCm, float Seconds) = 0;
	virtual float EndBodyFlight(float Seconds) = 0;
	virtual float BeginBodySummon(const FTDMonsterSummonRequest& Request) = 0;
	virtual int32 CountBodyMinionsAlive() const = 0;
	virtual void NotifyBodyPhaseEntered(int32 PhaseIndex, FName PhaseId) = 0;
	virtual void SetBodyChanneling(bool bIsChanneling) = 0;
	virtual void SetBodyDashing(bool bIsDashing) = 0;
};
