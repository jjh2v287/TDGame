#pragma once

#include "CoreMinimal.h"
#include "Components/PointLightComponent.h"
#include "TDFluidFireLightComponent.generated.h"

class UTDFluidLiveComponent;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TDGAME_API UTDFluidFireLightComponent : public UPointLightComponent
{
	GENERATED_BODY()

public:
	UTDFluidFireLightComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireLight")
	bool bIsDrivenByFluid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireLight")
	bool bUsesSelectiveDensity = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireLight", meta = (ClampMin = "0.0"))
	float MaxIntensityCandelas = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireLight", meta = (ClampMin = "1.0"))
	float ReferenceDensityAreaCm2 = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireLight", meta = (ClampMin = "0.1"))
	float ResponsePerSec = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireLight", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlickerNoiseAmplitude = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireLight", meta = (ClampMin = "0.1", ClampMax = "30.0"))
	float FlickerNoiseFrequencyHz = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireLight", meta = (ClampMin = "1000.0", ClampMax = "12000.0"))
	float MinColorTemperatureKelvin = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireLight", meta = (ClampMin = "1000.0", ClampMax = "12000.0"))
	float MaxColorTemperatureKelvin = 2100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireLight")
	float HeightOffsetCm = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireLight", meta = (ClampMin = "0.0"))
	float MoveThresholdCm = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FireLight")
	int32 RandomSeed = 1;

	void UpdateFromFluid(const UTDFluidLiveComponent& LiveComponent, const FVector& AnchorWorld, float DeltaTime);

	UFUNCTION(BlueprintPure, Category = "FireLight")
	float GetLastDensityAreaCm2() const { return LastDensityAreaCm2; }

	UFUNCTION(BlueprintPure, Category = "FireLight")
	float GetLastTargetIntensity() const { return LastTargetIntensity; }

	UFUNCTION(BlueprintPure, Category = "FireLight")
	float GetLastIntensityCandelas() const { return LastIntensityCandelas; }

private:
	void ApplyLightPosition(const UTDFluidLiveComponent& LiveComponent, const FVector& AnchorWorld, const FVector2D& DensityCentroidPlane, bool bHasDensity);

	double ElapsedSeconds = 0.0;
	float LastDensityAreaCm2 = 0.0f;
	float LastTargetIntensity = 0.0f;
	float LastIntensityCandelas = 0.0f;
};
