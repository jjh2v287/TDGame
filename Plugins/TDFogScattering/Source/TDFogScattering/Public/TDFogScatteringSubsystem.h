#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"

#include "TDFogScatteringSubsystem.generated.h"

class FTDFogScatteringViewExtension;

UCLASS()
class TDFOGSCATTERING_API UTDFogScatteringSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "TD|Fog Scattering")
	void SetFogScatteringEnabled(bool bIsEnabled);

	UFUNCTION(BlueprintPure, Category = "TD|Fog Scattering")
	bool IsFogScatteringEnabled() const;

	UFUNCTION(BlueprintCallable, Category = "TD|Fog Scattering")
	void SetFogScatteringIntensity(float Intensity);

	UFUNCTION(BlueprintPure, Category = "TD|Fog Scattering")
	float GetFogScatteringIntensity() const;

	UFUNCTION(BlueprintCallable, Category = "TD|Fog Scattering")
	void SetFogScatteringQualityPreset(int32 QualityPreset);

	UFUNCTION(BlueprintPure, Category = "TD|Fog Scattering")
	int32 GetFogScatteringQualityPreset() const;

	UFUNCTION(BlueprintCallable, Category = "TD|Fog Scattering")
	void SetFogScatteringDistanceScale(float DistanceScale);

	UFUNCTION(BlueprintPure, Category = "TD|Fog Scattering")
	float GetFogScatteringDistanceScale() const;

	UFUNCTION(BlueprintCallable, Category = "TD|Fog Scattering")
	void SetFogScatteringWeightPower(float WeightPower);

	UFUNCTION(BlueprintPure, Category = "TD|Fog Scattering")
	float GetFogScatteringWeightPower() const;

private:
	TSharedPtr<FTDFogScatteringViewExtension, ESPMode::ThreadSafe> ViewExtension;
};
