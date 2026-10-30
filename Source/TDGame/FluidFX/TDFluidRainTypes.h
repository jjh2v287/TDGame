#pragma once

#include "CoreMinimal.h"
#include "TDFluidRainTypes.generated.h"

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidRainSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "400.0"))
	float DropsPerSquareMeterPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0", ClampMax = "15.0"))
	float DropRadiusCm = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float DropRippleImpulseCm = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropDensityStrength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "512"))
	int32 MaxDropsPerTick = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float NearDropImpulseCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WetnessFromRainPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bShouldAccumulatePuddles = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float PuddleFillCmPerSec = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float PuddleDrainCmPerSec = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float PuddleMaxLevelCm = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "20.0", ClampMax = "600.0"))
	float PuddleCavityRadiusCm = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.5", ClampMax = "50.0"))
	float PuddleCavityReferenceCm = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "2147483647"))
	int32 RandomSeed = 1;
};
