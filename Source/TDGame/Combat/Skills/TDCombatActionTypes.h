#pragma once

#include "CoreMinimal.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "TDCombatActionTypes.generated.h"

class UAnimSequence;

UENUM(BlueprintType)
enum class ETDCombatHitExecutionType : uint8
{
	TimelineSweep UMETA(DisplayName="Timeline Sweep"),
	DirectDamage UMETA(DisplayName="Direct Damage")
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDActionTimeWindow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Window", meta=(ClampMin="0.0", ForceUnits="s"))
	float StartSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Window", meta=(ClampMin="0.0", ForceUnits="s"))
	float EndSeconds = 0.f;

	bool IsValid() const { return EndSeconds > StartSeconds; }
	bool Contains(const float AnimationTime) const { return IsValid() && AnimationTime >= StartSeconds && AnimationTime < EndSeconds; }
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDMeleeSweepSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FName WeaponBaseSocketName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FName WeaponTipSocketName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon", meta=(ClampMin="2", ClampMax="16"))
	int32 BladeSampleCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sweep", meta=(ClampMin="0.1"))
	float SweepRadius = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sweep")
	TEnumAsByte<ECollisionChannel> SweepChannel = ECC_Pawn;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sweep", meta=(ClampMin="0.001", ClampMax="0.1"))
	float SampleIntervalSeconds = 1.f / 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sweep", meta=(ToolTip="Blade points keep the owner's height (actor location Z plus offset) so the sweep stays on the top-down plane even when the animation swings up or down."))
	bool bLockHeightToOwner = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sweep", meta=(EditCondition="bLockHeightToOwner", ForceUnits="cm"))
	float LockedHeightOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	ETDDamageTargetPolicy TargetPolicy = ETDDamageTargetPolicy::Enemies;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	TArray<FTDDamageRule> HitRules;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug")
	bool bDrawDebugSweep = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug", meta=(ClampMin="0", EditCondition="bDrawDebugSweep"))
	float DebugDrawDuration = 1.f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDMeleeHitWindow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Window")
	FTDActionTimeWindow Window;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sweep")
	FTDMeleeSweepSettings Sweep;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDTagWindow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Window")
	FTDActionTimeWindow Window;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GameplayTags")
	FGameplayTagContainer Tags;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDActionAnimation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	TSoftObjectPtr<UAnimSequence> Animation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ClampMin="0.0", ForceUnits="s", ToolTip="Animation time the action starts from."))
	float StartSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ClampMin="0.0", ForceUnits="s", ToolTip="Animation time the action ends at. 0 plays to the end of the sequence."))
	float EndSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ClampMin="0.1"))
	float PlayRate = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	bool bUseRootMotion = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timeline", meta=(ToolTip="Melee sweeps in animation time."))
	TArray<FTDMeleeHitWindow> HitWindows;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timeline", meta=(ToolTip="Gameplay tags added to the owner while the animation time is inside the window."))
	TArray<FTDTagWindow> TagWindows;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timeline", meta=(ToolTip="Combat input is buffered while the animation time is inside this window."))
	FTDActionTimeWindow InputBufferWindow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timeline", meta=(ToolTip="Capsule jump modifier is active while the animation time is inside this window."))
	FTDActionTimeWindow JumpCapsuleWindow;

	bool HasAnimation() const { return !Animation.IsNull(); }
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDCombatActionDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	FTDActionAnimation Action;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat", meta=(ClampMin="0.0"))
	float Range = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage", meta=(ClampMin="0.0"))
	float DamageMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	float FlatDamageBonus = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	ETDDamageElement DamageElement = ETDDamageElement::Physical;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Execution")
	ETDCombatHitExecutionType HitExecutionType = ETDCombatHitExecutionType::TimelineSweep;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Execution")
	bool bRotateToTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Execution")
	bool bRequiresTarget = true;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDCombatReactionDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	FTDActionAnimation Action;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction", meta=(ClampMin="0.0"))
	float StaggerDuration = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	FVector KnockbackImpulse = FVector::ZeroVector;
};
