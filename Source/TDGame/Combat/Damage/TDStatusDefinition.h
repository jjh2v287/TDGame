#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "TDStatusDefinition.generated.h"

UCLASS(BlueprintType)
class TDGAME_API UTDStatusDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Status", meta=(ClampMin="0.02"))
	float Duration = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Status", meta=(ClampMin="0.02"))
	float PulseInterval = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Status")
	bool bFreezesTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildup", meta=(ClampMin="0"))
	float DamageThreshold = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildup", meta=(ClampMin="0.02"))
	float BuildupResetDelay = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Status")
	bool bRefreshDuration = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Events")
	TArray<FTDDamageRule> Rules;

	bool ValidateDefinition(FString& OutError) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
