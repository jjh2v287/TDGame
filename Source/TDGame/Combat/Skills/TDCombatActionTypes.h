#pragma once

#include "CoreMinimal.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "TDCombatActionTypes.generated.h"

class UAnimMontage;

UENUM(BlueprintType)
enum class ETDCombatHitExecutionType : uint8
{
	NotifyTrace UMETA(DisplayName="Notify Trace"),
	DirectDamage UMETA(DisplayName="Direct Damage")
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDCombatActionDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	TSoftObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	FName MontageSection = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ClampMin="0.1"))
	float PlayRate = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat", meta=(ClampMin="0.0"))
	float Range = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage", meta=(ClampMin="0.0"))
	float DamageMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	float FlatDamageBonus = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	ETDDamageElement DamageElement = ETDDamageElement::Physical;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Execution")
	ETDCombatHitExecutionType HitExecutionType = ETDCombatHitExecutionType::NotifyTrace;

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
	TSoftObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	FName MontageSection = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ClampMin="0.1"))
	float PlayRate = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction", meta=(ClampMin="0.0"))
	float StaggerDuration = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reaction")
	FVector KnockbackImpulse = FVector::ZeroVector;
};
