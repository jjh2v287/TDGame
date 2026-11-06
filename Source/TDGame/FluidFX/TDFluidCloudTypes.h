#pragma once

#include "CoreMinimal.h"
#include "TDFluidCloudTypes.generated.h"

UENUM(BlueprintType)
enum class ETDFluidCloudPainterProjection : uint8
{
	LayerMidHeight,
	Terrain
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidCloudSceneSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudTracing", meta = (ClampMin = "0.25", ClampMax = "8.0"))
	float ViewSampleCountScaleFar = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudTracing", meta = (ClampMin = "4.0", ClampMax = "6.0"))
	float ViewSampleCountScaleNear = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudTracing", meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float ShadowViewSampleCountScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudTracing", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float NearEnterMarginKm = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudTracing", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float NearExitMarginKm = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudTracing", meta = (ClampMin = "0.1", ClampMax = "1000.0"))
	float TracingMaxDistanceKm = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudTracing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StopTracingTransmittanceThreshold = 0.005f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudPainter", meta = (ClampMin = "0.01", ClampMax = "50.0"))
	float PainterBrushRadiusKm = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudPainter", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float PainterVelocityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudPainter")
	ETDFluidCloudPainterProjection PainterProjection = ETDFluidCloudPainterProjection::LayerMidHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudPainter", meta = (ClampMin = "1", ClampMax = "64"))
	int32 PainterMaxStampsPerTick = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudWindCurtain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WindCurtainHeightRatio = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudWindCurtain", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float WindCurtainThicknessRatio = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudWindCurtain", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float WindCurtainSpanRatio = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudWindCurtain", meta = (ClampMin = "0.0", ClampMax = "1200.0"))
	float WindCurtainSpeedCmPerSec = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudWindCurtain", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float WindCurtainLiftRatio = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudWindCurtain", meta = (ClampMin = "1", ClampMax = "64"))
	int32 WindCurtainStampCount = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudWindCurtain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WindCurtainGustAmplitude = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudWindCurtain", meta = (ClampMin = "0.5", ClampMax = "120.0"))
	float WindCurtainGustPeriodSeconds = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudCyclone", meta = (ClampMin = "0.005", ClampMax = "0.3"))
	float CycloneEyeRadiusRatio = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CloudCyclone", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float CycloneEyeEraseStrength = 1.0f;
};
