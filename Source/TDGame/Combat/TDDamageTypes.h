#pragma once

#include "CoreMinimal.h"
#include "TDDamageTypes.generated.h"

class UTDDamageDefinition;
class UTDStatusDefinition;

UENUM(BlueprintType)
enum class ETDDamageElement : uint8
{
	Physical,
	Fire,
	Frost,
	Arcane
};

UENUM(BlueprintType)
enum class ETDDamageEvent : uint8
{
	Spawn,
	Hit,
	Pulse,
	Trigger,
	Expire,
	End,
	Kill,
	Activate
};

UENUM(BlueprintType)
enum class ETDDamageActionType : uint8
{
	Damage,
	ApplyStatus,
	SpawnEntity,
	ApplyHoming,
	StopHoming
};

UENUM(BlueprintType)
enum class ETDDamageEntityMode : uint8
{
	Projectile,
	Area,
	Mine,
	Shockwave
};

UENUM(BlueprintType)
enum class ETDDamageTargetPolicy : uint8
{
	Enemies,
	Allies,
	Everyone
};

UENUM(BlueprintType)
enum class ETDDamageSpawnAnchor : uint8
{
	EventLocation,
	Target,
	CastTarget
};

UENUM(BlueprintType)
enum class ETDDamageDirection : uint8
{
	Inherit,
	TowardCastTarget,
	Down
};

UENUM(BlueprintType)
enum class ETDHomingTargetSelection : uint8
{
	NearestEnemy,
	EventTarget
};

UENUM(BlueprintType)
enum class ETDHomingTargetLossPolicy : uint8
{
	Reacquire,
	ContinueStraight
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDHomingSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ETDHomingTargetSelection TargetSelection = ETDHomingTargetSelection::NearestEnemy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"))
	float SearchRadius = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.01"))
	float TurnRateDegreesPerSecond = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.02"))
	float RetargetInterval = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ETDHomingTargetLossPolicy TargetLossPolicy = ETDHomingTargetLossPolicy::Reacquire;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bRetargetOnApply = true;

	bool Validate(FString& OutError) const;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDCombatStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="1000"))
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 TeamId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"))
	float BaseMaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float HealthPerLevel = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float AttackPower = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float SpellPower = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float AttackPowerPerLevel = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float SpellPowerPerLevel = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float Armor = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float MagicResistance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1"))
	float CriticalChance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1"))
	float CriticalMultiplier = 1.5f;

	float GetMaxHealth() const;
	float GetAttackPower() const;
	float GetSpellPower() const;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDScaledValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float Base = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float PerLevel = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float AttackRatio = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float SpellRatio = 0.f;

	float Evaluate(const FTDCombatStats& Stats) const;
};

struct FTDDamageChainBudget
{
	int32 RemainingActions = 2048;
	int32 RemainingSpawns = 128;
};

USTRUCT()
struct TDGAME_API FTDDamageContext
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> Caster;

	UPROPERTY()
	FTDCombatStats Stats;

	UPROPERTY()
	FVector CastTarget = FVector::ZeroVector;

	UPROPERTY()
	FVector Direction = FVector::ForwardVector;

	UPROPERTY()
	int32 Depth = 0;

	TSharedPtr<FTDDamageChainBudget> Budget;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDDamageAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ETDDamageActionType Type = ETDDamageActionType::Damage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float DelaySeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(EditCondition="Type == ETDDamageActionType::ApplyHoming", EditConditionHides))
	FTDHomingSettings Homing;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTDScaledValue Magnitude;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ETDDamageElement Element = ETDDamageElement::Physical;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bCanCrit = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UTDStatusDefinition> Status;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UTDDamageDefinition> Entity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ETDDamageSpawnAnchor SpawnAnchor = ETDDamageSpawnAnchor::EventLocation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ETDDamageDirection SpawnDirection = ETDDamageDirection::Inherit;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector SpawnOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="32"))
	int32 SpawnCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	float ScatterRadius = 0.f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDDamageRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ETDDamageEvent Event = ETDDamageEvent::Hit;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FTDDamageAction> Actions;

	bool Validate(FString& OutError) const;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDDamageResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	float AppliedDamage = 0.f;

	UPROPERTY(BlueprintReadOnly)
	bool bWasCritical = false;

	UPROPERTY(BlueprintReadOnly)
	bool bWasKilled = false;
};
