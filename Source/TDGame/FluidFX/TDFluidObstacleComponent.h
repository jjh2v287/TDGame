#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidObstacleComponent.generated.h"

UCLASS(ClassGroup = (FluidFX), meta = (BlueprintSpawnableComponent))
class TDGAME_API UTDFluidObstacleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTDFluidObstacleComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bUseOwnerBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	FVector BoxHalfExtentsCm = FVector(50.0, 50.0, 50.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector BoxOffsetCm = FVector::ZeroVector;

	bool BuildObstacle(FTDFluidSdfObstacle& OutObstacle) const;
	static FTDFluidSdfObstacle MakeOrientedBoxObstacle(const FVector& WorldCenter, const FVector& HalfExtentsCm, const FRotator& WorldRotation);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleRootTransformUpdated(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);
	void MarkFluidObstaclesDirty() const;

	TWeakObjectPtr<USceneComponent> BoundRootComponent;
	FDelegateHandle RootTransformUpdatedHandle;
};
