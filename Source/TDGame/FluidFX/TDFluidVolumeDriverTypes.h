#pragma once

#include "CoreMinimal.h"
#include "TDFluidVolumeDriverTypes.generated.h"

UENUM(BlueprintType)
enum class ETDFluidOutputMaterialSlot : uint8
{
	Primary,
	Secondary,
	Tertiary
};

UENUM(BlueprintType)
enum class ETDFluidTerrainHeightSource : uint8
{
	LineTrace,
	HeightCache
};

UENUM(BlueprintType)
enum class ETDFluidVolumeHeightSource : uint8
{
	None,
	HeightCache,
	SceneCapture
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidVolumeDriverSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bRenderHeterogeneousVolume = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidOutputMaterialSlot HeterogeneousVolumeMaterialSlot = ETDFluidOutputMaterialSlot::Secondary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "10.0", ClampMax = "100000.0"))
	float HeterogeneousVolumeHeightCm = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float HeterogeneousVolumeSizeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector HeterogeneousVolumeOffsetCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "16", ClampMax = "1024"))
	int32 HeterogeneousVolumeResolutionXY = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "8", ClampMax = "512"))
	int32 HeterogeneousVolumeResolutionZ = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.1", ClampMax = "8.0"))
	float HeterogeneousVolumeStepFactor = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.1", ClampMax = "8.0"))
	float HeterogeneousVolumeShadowStepFactor = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float HeterogeneousVolumeShadowBiasFactor = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0", ClampMax = "8.0"))
	float HeterogeneousVolumeLightingDownsampleFactor = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bHeterogeneousVolumeCastShadow = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1000000.0"))
	float HeterogeneousVolumeMaxDrawDistanceCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bRenderFogVolume = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidOutputMaterialSlot FogVolumeMaterialSlot = ETDFluidOutputMaterialSlot::Tertiary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "10.0", ClampMax = "100000.0"))
	float FogVolumeHeightCm = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0", ClampMax = "8.0"))
	float FogVolumeSizeMultiplier = 1.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidOutputTargetSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "15"))
	int32 PrimaryOutputMaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "15"))
	int32 SecondaryOutputMaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "15"))
	int32 TertiaryOutputMaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bRecollectTargetsDuringGame = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float RecollectIntervalSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bDriveTaggedMeshes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bDriveTaggedHeterogeneousVolumes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bRestoreTargetMaterialsOnEnd = true;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidTerrainHeightSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidTerrainHeightSource HeightSource = ETDFluidTerrainHeightSource::LineTrace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "100000.0"))
	float TraceStartHeightCm = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "100000.0"))
	float TraceEndDepthCm = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "256", ClampMax = "1048576"))
	int32 MaxCacheSamplesPerTick = 65536;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bClampHeightLowerValues = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-100000.0", ClampMax = "100000.0"))
	float ClampingValueCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-100000.0", ClampMax = "100000.0"))
	float HeightOffsetCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bUseHeightAsCollisionMask = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "100000.0"))
	float CollisionHeightAboveBaseCm = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1", ClampMax = "4096"))
	int32 MaxCollisionBoxes = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidVolumeHeightSource VolumeHeightSource = ETDFluidVolumeHeightSource::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "64", ClampMax = "2048"))
	int32 SceneCaptureResolution = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "100.0", ClampMax = "100000.0"))
	float SceneCaptureHeightAboveCm = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1", ClampMax = "64"))
	int32 SceneCaptureRecaptureCells = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SceneCaptureMarginRatio = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bSceneCaptureIgnoresPawns = true;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidHeterogeneousVolumeRenderProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-1", ClampMax = "4096"))
	int32 MaxStepCount = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-1", ClampMax = "1024"))
	int32 VolumeResolutionZ = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-1", ClampMax = "1"))
	int32 Preshading = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-1", ClampMax = "16"))
	int32 LightingCacheDownsampleFactor = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-1", ClampMax = "1"))
	int32 Shadows = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-1.0", ClampMax = "16.0"))
	float IndirectLighting = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-1", ClampMax = "1"))
	int32 AmbientOcclusion = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-1.0", ClampMax = "1000000.0"))
	float MaxTraceDistance = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-1", ClampMax = "1"))
	int32 Composition = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-1", ClampMax = "1"))
	int32 VolumetricFog = -1;
};
