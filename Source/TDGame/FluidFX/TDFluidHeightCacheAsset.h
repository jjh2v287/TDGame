#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TDFluidHeightCacheAsset.generated.h"

class UTexture2D;
class UWorld;
struct FCollisionQueryParams;

UCLASS(BlueprintType)
class TDGAME_API UTDFluidHeightCacheAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FVector2D OriginCm = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FluidFX", meta = (ClampMin = "0"))
	int32 Resolution = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float TexelCm = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float EncodingMinZCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float EncodingRangeCm = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "FluidFX")
	TArray<uint16> EncodedHeights;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FString SourceLevel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	int64 SourceChecksum = 0;

	static constexpr uint16 MissEncodedHeight = 0;
	static constexpr float MissTextureHeightCm = -1.0e9f;
	static constexpr int32 MaxBakeResolution = 4096;

	static uint16 EncodeHeight(float HeightCm, float MinZCm, float RangeCm);
	static float DecodeHeight(uint16 EncodedHeight, float MinZCm, float RangeCm);
	static float GetQuantizationErrorCm(float RangeCm);
	static void AddBakeIgnoredActors(UWorld& World, FCollisionQueryParams& InOutQueryParams);

	bool IsUsable() const;
	bool SampleHeightCm(const FVector2D& WorldXY, double& OutHeightCm) const;
	FVector4f GetHeightWindow() const;
	UTexture2D* CreateTransientHeightTexture() const;
	void SetEncodedFromHeights(const FVector2D& InOriginCm, int32 InResolution, float InTexelCm, TConstArrayView<float> HeightsCm, TConstArrayView<bool> HitMask);

	UFUNCTION(BlueprintCallable, Category = "FluidFX", meta = (WorldContext = "WorldContextObject"))
	bool BakeFromLineTraces(UObject* WorldContextObject, FVector2D CenterCm, FVector2D ExtentCm, float InTexelCm, float TraceTopZCm, float TraceBottomZCm);
};
