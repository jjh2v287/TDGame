#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidHullWakeComponent.generated.h"

class ATDFluidWaterActor;

UCLASS(ClassGroup = (FluidFX), meta = (BlueprintSpawnableComponent))
class TDGAME_API UTDFluidHullWakeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTDFluidHullWakeComponent();

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "FluidFX|HullWake")
	TObjectPtr<ATDFluidWaterActor> TargetWaterActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX|HullWake", meta = (ClampMin = "0.0"))
	float HullLengthCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX|HullWake", meta = (ClampMin = "0.0"))
	float HullBeamCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX|HullWake")
	bool bDrivesWaterWindow = true;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void CacheOwnerLocalBounds();
	void QueueHullStamps(float DeltaTime);

	FVector LocalHullBoundsCenter = FVector::ZeroVector;
	FVector LocalHullBoundsSize = FVector::ZeroVector;
	TOptional<FVector> PreviousHullCenter;
	TArray<FTDFluidBrushStamp> HullStamps;
};
