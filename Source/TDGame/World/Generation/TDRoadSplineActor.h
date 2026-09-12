#pragma once

#include "GameFramework/Actor.h"
#include "TDRoadSplineActor.generated.h"

class USplineComponent;

UCLASS()
class TDGAME_API ATDRoadSplineActor : public AActor
{
	GENERATED_BODY()

public:
	ATDRoadSplineActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TD|World")
	TObjectPtr<USplineComponent> Spline;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TD|World")
	FName RoadId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TD|World", meta = (ClampMin = "0"))
	float WidthCm = 600.0f;

	UFUNCTION(BlueprintCallable, Category = "TD|World")
	void SetRoadPoints(const TArray<FVector>& WorldPointsCm);
};
