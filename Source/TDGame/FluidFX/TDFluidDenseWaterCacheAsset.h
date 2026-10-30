#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TDFluidDenseWaterCacheAsset.generated.h"

struct FTDFluidDenseWaterSeed
{
	int32 Resolution = 0;
	FVector2D CenterCm = FVector2D::ZeroVector;
	FVector2D ExtentCm = FVector2D::ZeroVector;
	TArray<float> DepthCm;
	TArray<float> FaceVelocityXCmPerSec;
	TArray<float> FaceVelocityYCmPerSec;
};

UCLASS(BlueprintType)
class TDGAME_API UTDFluidDenseWaterCacheAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FluidFX", meta = (ClampMin = "0"))
	int32 Resolution = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FVector2D CenterCm = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FVector2D ExtentCm = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float TerrainReferenceZCm = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "FluidFX")
	TArray<float> DepthCm;

	UPROPERTY(VisibleAnywhere, Category = "FluidFX")
	TArray<float> FaceVelocityXCmPerSec;

	UPROPERTY(VisibleAnywhere, Category = "FluidFX")
	TArray<float> FaceVelocityYCmPerSec;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float SimulatedSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	int64 SourceChecksum = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FString SourceLevel;

	bool IsUsable() const;
	bool MatchesWindow(const FVector2D& WindowCenterCm, const FVector2D& WindowExtentCm) const;
	void SetFromSeed(const FTDFluidDenseWaterSeed& Seed);
	FTDFluidDenseWaterSeed MakeSeed() const;
};
