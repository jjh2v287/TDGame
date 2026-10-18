#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidFireTypes.generated.h"

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidSourceEmitterSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bIsEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidBrushChannel Channel = ETDFluidBrushChannel::ChannelB_Fire;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float RadiusCm = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DensityStrength = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1200.0"))
	float EmitSpeedCmPerSec = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MotionInertiaScale = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlickerAmplitude = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.1", ClampMax = "30.0"))
	float FlickerFrequencyHz = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector SourceOffsetCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1", ClampMax = "8"))
	int32 SourceStampCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float SourceSpreadCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TrailLeadSeconds = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float MaxTrailLeadRatio = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	int32 RandomSeed = 1;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidDownstreamFeedSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bIsEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float BandStartRatio = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinFeedDensity = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FadedDensityThreshold = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxStampsPerStep = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DensityScale = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.1"))
	float RadiusScale = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float VelocityInheritance = 1.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidFragmentTrailSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.1"))
	float TrackingDurationSeconds = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float ArmedPollIntervalSeconds = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bShouldCrumbleOnTrigger = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float MinFragmentSpeedCmPerSec = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float MaxFragmentSpeedCmPerSec = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float FireStampRadiusCm = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FireDensityStrength = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float SmokeStampRadiusCm = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmokeDensityStrength = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "7"))
	int32 SmokeTrailLagFrames = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1"))
	int32 MaxFragmentsPerStep = 48;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1"))
	int32 MaxStampsPerFragment = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float VelocityInheritance = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float BurstCoreRadiusCm = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float BurstRingRadiusCm = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "32"))
	int32 BurstRingStampCount = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float BurstRingSpeedCmPerSec = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float BurstSustainSeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "512"))
	int32 EmberBurstCount = 80;
};
