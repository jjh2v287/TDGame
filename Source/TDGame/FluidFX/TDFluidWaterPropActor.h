#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidWaterPropTypes.h"
#include "Templates/Function.h"
#include "UObject/ObjectKey.h"
#include "TDFluidWaterPropActor.generated.h"

struct FTDFluidWaterPropPlaneStamp
{
	FVector2D PlanePointCm = FVector2D::ZeroVector;
	FVector2D PlaneVelocityCmPerSec = FVector2D::ZeroVector;
	float RadiusCm = 0.0f;
	float DensityStrength = 0.0f;
	float RadialVelocityCmPerSec = 0.0f;
	float RippleImpulseCm = 0.0f;
};

struct FTDFluidWaterPropCouplingColumn
{
	FIntPoint CenterCell = FIntPoint::ZeroValue;
	float MeanDensity = 0.0f;
	float MeanDownSpeedCmPerSec = 0.0f;
};

UCLASS()
class TDGAME_API ATDFluidWaterPropActor : public ATDFluidLiveActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "WaterProp")
	TObjectPtr<ATDFluidLiveActor> CouplingReceiverActor;

	virtual void BeginPlay() override;
	virtual void ReapplyPresetTuning(FTDFluidPresetReapplyReport& InOutReport) override;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FTDFluidWaterPropStats GetWaterPropStats() const { return WaterPropStats; }

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FTDFluidWaterPropSettings GetWaterPropSettings() const { return WaterPropSettings; }

	static void BuildSourcePlaneStamps(const FTDFluidWaterPropSettings& Settings, const FVector2D& WindowCenterPlane, const FVector2D& ExtentCm, double TimeSeconds, TArray<FTDFluidWaterPropPlaneStamp>& OutStamps);
	static void SelectCouplingColumns(int32 Resolution, const FTDFluidWaterPropSettings& Settings, TFunctionRef<FTDFluidCellSample(int32, int32)> SampleCell, TArray<FTDFluidWaterPropCouplingColumn>& OutColumns);

protected:
	virtual void PrepareActorSpecificInputs(float DeltaTime) override;
	virtual void FinishActorSpecificOutputs(float DeltaTime) override;
	virtual bool HasOffscreenOutputConsumers() const override;

private:
	void RefreshWaterPropSettings(FTDFluidPresetReapplyReport* InOutReport);
	bool HasPresetChangedSinceRefresh() const;
	void QueueSourceStamps();
	void QueueCouplingStamps();
	bool CanQueueCouplingInto(const ATDFluidLiveActor& ReceiverActor);

	FTDFluidWaterPropSettings WaterPropSettings;
	FTDFluidWaterPropStats WaterPropStats;
	FObjectKey SettingsSourcePresetKey;
	double PropSimulationTimeSeconds = 0.0;
	bool bHasRefreshedWaterPropSettings = false;
	bool bHasWarnedSelfCouplingReceiver = false;
};
