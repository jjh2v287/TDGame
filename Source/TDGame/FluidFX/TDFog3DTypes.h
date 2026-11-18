#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "TDFog3DTypes.generated.h"

class UMaterialInterface;

USTRUCT(BlueprintType)
struct TDGAME_API FTDFog3DSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	int32 Mode = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	int32 DefaultQualityLevel = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	int32 QualityLowResolution = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float QualityLowStepsPerSecond = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	int32 QualityMediumResolution = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float QualityMediumStepsPerSecond = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	int32 QualityHighResolution = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float QualityHighStepsPerSecond = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float CellSizeCm = 15.625f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float LayerThicknessCm = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float MaximumStepSeconds = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	int32 WindowFollowMarginCells = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float BaseDensity = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float BaseThicknessCm = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float BaseTopBlurCm = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float WindRefillPerSec = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float DisplacementRefillHorizontalPerSec = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float DisplacementRefillVerticalPerSec = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float FillRefillPerSec = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	int32 EdgeBandCells = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float EdgeBandAbsorptionPerSec = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	FName BladeSocketName = TEXT("HandGrip_R");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	FVector BladeBaseLocalCm = FVector(-1.15, 3.8, 6.06);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	FVector BladeTipLocalCm = FVector(-13.37, 28.72, 98.11);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float BladeRadiusCm = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float BladeWindStrength = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float BladeWindGain = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float BladeErosionPerSec = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	FName HandBoneName = TEXT("hand_r");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	FName LeftFootBoneName = TEXT("foot_l");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	FName RightFootBoneName = TEXT("foot_r");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	FName PelvisBoneName = TEXT("pelvis");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float HandRadiusCm = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float FootRadiusCm = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float PelvisRadiusCm = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float BodyWindStrength = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float BodyWindGain = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float BodyErosionPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float MaximumInputSpeedCmPerSec = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	bool bUsesClampedTimeWindBlend = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	bool bOccludesInputBehindObstacles = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	int32 MaximumInputSphereCount = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	int32 MaximumObstacleCount = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float ObstacleBaseHalfThicknessCm = 7.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float ObstacleUpwardExtraCm = 7.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float ZoneEdgeFadeCm = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float ZoneEntryVerticalMarginCm = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	bool bFineNoiseEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float FineNoiseAmplitude = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float FineNoiseSpacingCm = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	int32 FineNoiseSeed = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	bool bStainNoiseEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float StainNoiseAmplitude = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float StainNoiseSpacingCm = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	int32 StainNoiseSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	bool bFollowsGround = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float GroundWallStepCm = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	int32 GroundCaptureBorderCells = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float GroundCaptureHeightAboveZoneCm = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	int32 GroundCaptureLatencyFrames = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float GroundMissingHeightCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	float GroundMissingBelowFloorCm = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")
	TSoftObjectPtr<UMaterialInterface> GroundFollowVolumeMaterial;
};

struct FTDFog3DQualityTier
{
	int32 Resolution = 0;
	float StepsPerSecond = 0.0f;
};

TDGAME_API void CollectTDFog3DSettingsProblems(const FTDFog3DSettings& Settings, TArray<FString>& OutProblems);
TDGAME_API void CollectTDFog3DZoneProblems(const FVector& ZoneHalfExtentCm, const FTDFog3DSettings& Settings, TArray<FString>& OutProblems);
TDGAME_API FTDFog3DQualityTier ResolveTDFog3DQualityTier(const FTDFog3DSettings& Settings, int32 Level);
