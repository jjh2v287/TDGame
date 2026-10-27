#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "TDFluidTrackSourceComponent.generated.h"

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidTrackSourcePoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrackSource")
	FName SocketName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrackSource")
	FVector LocalOffsetCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrackSource", meta = (ClampMin = "1.0", ClampMax = "400.0"))
	float RadiusCm = 15.0f;
};

UCLASS(ClassGroup = (FluidFX), meta = (BlueprintSpawnableComponent))
class TDGAME_API UTDFluidTrackSourceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrackSource")
	TArray<FTDFluidTrackSourcePoint> Points;

	UFUNCTION(BlueprintPure, Category = "TrackSource")
	bool ResolvePointWorldLocation(int32 PointIndex, FVector& OutWorldLocation) const;

	float ResolvePointRadiusCm(int32 PointIndex, float RadiusScale) const;
};
