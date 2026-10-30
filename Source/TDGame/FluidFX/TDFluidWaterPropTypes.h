#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidWaterPropTypes.generated.h"

UENUM(BlueprintType)
enum class ETDFluidWaterPropSourceShape : uint8
{
	Point,
	Line
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidWaterPropSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bEnableSource = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidWaterPropSourceShape SourceShape = ETDFluidWaterPropSourceShape::Point;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-0.5", ClampMax = "0.5"))
	FVector2D SourceWindowRatio = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "4000.0"))
	float SourceLengthCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1", ClampMax = "16"))
	int32 SourceStampCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float SourceRadiusCm = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SourceDensityStrength = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-1200.0", ClampMax = "1200.0"))
	FVector2D SourceVelocityCmPerSec = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-1200.0", ClampMax = "1200.0"))
	float SourceRadialVelocityCmPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-100.0", ClampMax = "100.0"))
	float SourceRippleImpulseCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float SourcePulseFrequencyHz = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SourcePulseAmplitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidBrushChannel SourceChannel = ETDFluidBrushChannel::ChannelA_Water;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	int32 RandomSeed = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bEnableOutflowCoupling = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.02", ClampMax = "0.4"))
	float CouplingBandRatio = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1", ClampMax = "32"))
	int32 CouplingColumnCount = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CouplingMinDensity = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "5.0", ClampMax = "500.0"))
	float CouplingRadiusCm = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CouplingDensityScale = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1200.0"))
	float CouplingRadialVelocityCmPerSec = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-100.0", ClampMax = "100.0"))
	float CouplingRippleImpulseCm = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float CouplingForwardOffsetCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidBrushChannel CouplingChannel = ETDFluidBrushChannel::ChannelA_Water;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidUnderwaterSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bEnableUnderwaterSwitch = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "5.0", ClampMax = "60.0"))
	float QueryRateHz = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float BlendSeconds = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float SurfaceMarginCm = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bUseWaterLayerReadback = true;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidWaterPropStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 SourceStampCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 CouplingStampCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 TotalSourceStamps = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 TotalCouplingStamps = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	float LastCouplingMeanDensity = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	float LastCouplingMeanDownSpeedCmPerSec = 0.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidUnderwaterStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	bool bIsCameraUnderwater = false;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	float CurrentBlendWeight = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	bool bHasSurface = false;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	float LastSurfaceHeightCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 QueryCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 SwitchCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 DrivenVolumeCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 TickCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	double TotalTickMilliseconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	float MaxTickMilliseconds = 0.0f;
};
