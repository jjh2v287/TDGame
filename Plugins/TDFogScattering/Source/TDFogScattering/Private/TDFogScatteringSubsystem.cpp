#include "TDFogScatteringSubsystem.h"

#include "SceneViewExtension.h"
#include "TDFogScatteringConsoleVariables.h"
#include "TDFogScatteringViewExtension.h"

void UTDFogScatteringSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ViewExtension = FSceneViewExtensions::NewExtension<FTDFogScatteringViewExtension>();
}

void UTDFogScatteringSubsystem::Deinitialize()
{
	ViewExtension.Reset();

	Super::Deinitialize();
}

void UTDFogScatteringSubsystem::SetFogScatteringEnabled(bool bIsEnabled)
{
	SetTDFogScatteringEnabledOnGameThread(bIsEnabled);
}

bool UTDFogScatteringSubsystem::IsFogScatteringEnabled() const
{
	return IsTDFogScatteringEnabledOnGameThread();
}

void UTDFogScatteringSubsystem::SetFogScatteringIntensity(float Intensity)
{
	SetTDFogScatteringIntensityOnGameThread(Intensity);
}

float UTDFogScatteringSubsystem::GetFogScatteringIntensity() const
{
	return GetTDFogScatteringIntensityOnGameThread();
}

void UTDFogScatteringSubsystem::SetFogScatteringQualityPreset(int32 QualityPreset)
{
	SetTDFogScatteringQualityPresetOnGameThread(QualityPreset);
}

int32 UTDFogScatteringSubsystem::GetFogScatteringQualityPreset() const
{
	return GetTDFogScatteringQualityPresetOnGameThread();
}

void UTDFogScatteringSubsystem::SetFogScatteringDistanceScale(float DistanceScale)
{
	SetTDFogScatteringDistanceScaleOnGameThread(DistanceScale);
}

float UTDFogScatteringSubsystem::GetFogScatteringDistanceScale() const
{
	return GetTDFogScatteringDistanceScaleOnGameThread();
}

void UTDFogScatteringSubsystem::SetFogScatteringWeightPower(float WeightPower)
{
	SetTDFogScatteringWeightPowerOnGameThread(WeightPower);
}

float UTDFogScatteringSubsystem::GetFogScatteringWeightPower() const
{
	return GetTDFogScatteringWeightPowerOnGameThread();
}
