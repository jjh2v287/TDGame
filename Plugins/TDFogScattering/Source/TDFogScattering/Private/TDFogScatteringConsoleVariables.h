#pragma once

#include "CoreMinimal.h"

struct FTDFogScatteringSettings
{
	bool bIsEnabled = true;
	int32 QualityPreset = 2;
	int32 Format = -1;
	int32 PassAmount = 8;
	float Radius = 0.9f;
	float Intensity = 1.0f;
	bool bShouldWeightLuminance = true;
	bool bShouldIncludeSkyAtmosphere = false;
	float SkyAtmosphereIntensity = 1.0f;
	bool bShouldIncludeVolumetricCloud = false;
	float VolumetricCloudIntensity = 1.0f;
	float VolumetricFogIntensity = 1.0f;
	bool bShouldIncludeHeterogeneousVolumes = true;
	float DistanceScale = 1.0f;
	float WeightPower = 1.0f;
	int32 DebugView = 0;
};

bool IsTDFogScatteringEnabledOnGameThread();
void SetTDFogScatteringEnabledOnGameThread(bool bIsEnabled);

float GetTDFogScatteringIntensityOnGameThread();
void SetTDFogScatteringIntensityOnGameThread(float Intensity);

int32 GetTDFogScatteringQualityPresetOnGameThread();
void SetTDFogScatteringQualityPresetOnGameThread(int32 QualityPreset);

float GetTDFogScatteringDistanceScaleOnGameThread();
void SetTDFogScatteringDistanceScaleOnGameThread(float DistanceScale);
float GetTDFogScatteringWeightPowerOnGameThread();
void SetTDFogScatteringWeightPowerOnGameThread(float WeightPower);

FTDFogScatteringSettings GetTDFogScatteringSettingsOnRenderThread();
