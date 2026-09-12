#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TDExclusionVolume.generated.h"

UCLASS()
class ATDExclusionVolume : public AActor
{
	GENERATED_BODY()

public:
	ATDExclusionVolume();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Exclusion", meta = (ClampMin = "0"))
	float RadiusCm = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Exclusion")
	FName Reason;
};
