#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TDRegionVolume.generated.h"

class UBoxComponent;

UCLASS()
class TDGAME_API ATDRegionVolume : public AActor
{
	GENERATED_BODY()

public:
	ATDRegionVolume();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Region")
	FName RegionId;

	UBoxComponent* GetRegionBounds() const { return RegionBounds; }

private:
	UPROPERTY(VisibleAnywhere, Category = "TD|Region")
	TObjectPtr<UBoxComponent> RegionBounds;
};
