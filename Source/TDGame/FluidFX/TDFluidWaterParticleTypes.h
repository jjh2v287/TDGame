#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "TDFluidWaterParticleTypes.generated.h"

class UTexture;

UENUM(BlueprintType)
enum class ETDFluidWaterSplashKind : uint8
{
	Entry,
	Exit,
	Move
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidWaterParticleSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bEmitSplashEvents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bApplyContactWhitewater = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bReadContactPreviousFrame = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bUseTerrainDepthSurfaceBase = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "50.0", ClampMax = "600.0"))
	float SplashEntrySpeedThresholdCmPerSec = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float SplashExitSpeedThresholdCmPerSec = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "2000.0"))
	float SplashMoveSpeedThresholdCmPerSec = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.02", ClampMax = "2.0"))
	float SplashMoveIntervalSeconds = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float SplashCooldownSeconds = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float SplashSurfaceBandCm = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0", ClampMax = "100.0"))
	float SplashBoneRadiusCm = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0", ClampMax = "100.0"))
	float SplashMinRadiusCm = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "5.0", ClampMax = "500.0"))
	float SplashMaxRadiusCm = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "256"))
	int32 MaxTrackedSplashPoints = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "64"))
	int32 MaxConcurrentSplashes = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "16000"))
	int32 MaxContactPointsPerTick = 2048;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "64"))
	int32 MaxContactStampsPerTick = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0", ClampMax = "200.0"))
	float ContactReadFallbackRadiusCm = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float DebrisPrefillSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.05", ClampMax = "5.0"))
	float CoverageRefreshSeconds = 0.5f;
};

USTRUCT()
struct TDGAME_API FTDFluidWaterSplashEvent
{
	GENERATED_BODY()

	UPROPERTY()
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector ImpactVelocity = FVector::ZeroVector;

	UPROPERTY()
	float ImpactSpeedCmPerSec = 0.0f;

	UPROPERTY()
	float RadiusCm = 0.0f;

	UPROPERTY()
	float SurfaceHeightCm = 0.0f;

	UPROPERTY()
	ETDFluidWaterSplashKind Kind = ETDFluidWaterSplashKind::Entry;
};

struct FTDFluidWaterSplashPointState
{
	FVector PreviousLocation = FVector::ZeroVector;
	float PreviousBottomHeightCm = 0.0f;
	float CooldownSeconds = 0.0f;
	float MoveIntervalSeconds = 0.0f;
	bool bHasPrevious = false;
};

struct FTDFluidWaterSurfaceParameters
{
	float BaseHeightCm = 0.0f;
	float UsesTerrainBase = 0.0f;
	float TerrainReferenceZCm = 0.0f;
	float DepthReferenceCm = 0.0f;
	float RippleHeightReferenceCm = 0.0f;
	float LayerActive = 0.0f;
	float BorderFadeRatio = 0.0f;
	float PressureNormalStrength = 0.0f;
	float PassiveWaveAmplitudeCm = 0.0f;
	float PassiveWaveDirectionDegrees = 0.0f;
	float TimeSeconds = 0.0f;
	float TileMapHeightCm = 0.0f;
	float TileMapDensityHeightCm = 0.0f;
	float TileMapFadeStartCm = 0.0f;
	float TileMapFadeLengthCm = 0.0f;
	float Coverage = 0.0f;
	FVector2D TileMapOriginCm = FVector2D::ZeroVector;
	FVector2D TileMapPeriodCm = FVector2D::ZeroVector;
	FVector CameraWorld = FVector::ZeroVector;
	TWeakObjectPtr<UTexture> TileMapTexture;
};
