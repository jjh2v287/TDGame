#pragma once

#include "CoreMinimal.h"
#include "Math/Float16Color.h"

struct FTDFluidTransmittanceInputs
{
	int32 SimResolution = 0;
	FVector2D ExtentCm = FVector2D::ZeroVector;
	float VolumeHeightCm = 150.0f;
	bool bIsSymmetric = false;
	float DensityScale = 0.035f;
	float SoftFalloffCm = 28.0f;
	int32 MarchStepCount = 12;
	int32 LightMode = 0;
	FVector LightPositionLocalCm = FVector::ZeroVector;
	FVector LightDirectionLocal = FVector(0.0, 0.0, -1.0);
};

namespace TDFluidVolumeTransmittance
{
	inline constexpr int32 LevelCount = 4;
	inline constexpr int32 ResolutionDivisor = 4;
	inline constexpr int32 MinimumResolution = 16;
	inline constexpr int32 MinimumMarchStepCount = 4;
	inline constexpr int32 MaximumMarchStepCount = 24;
	inline constexpr int32 PointLightMode = 1;
	inline constexpr int32 DirectionalLightMode = 2;
	inline constexpr float EdgeFadeFraction = 0.24f;
	inline constexpr float CoverageScale = 2.5f;
	inline constexpr float HeightScaleRatio = 0.68f;
	inline constexpr float TopFadeRatio = 0.15f;

	TDGAME_API int32 ComputeOutputResolution(int32 SimResolution);
	TDGAME_API float ComputeLevelHeightCm(float VolumeHeightCm, int32 LevelIndex);
	TDGAME_API float ComputeOpticalDepth(const FTDFluidTransmittanceInputs& Inputs, TConstArrayView<FFloat16Color> DensityPressurePixels, const FVector& StartLocalCm);
	TDGAME_API void ComputePixels(const FTDFluidTransmittanceInputs& Inputs, TConstArrayView<FFloat16Color> DensityPressurePixels, TArray<FFloat16Color>& OutPixels);
}
