#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidRuntimeControlComponent.generated.h"

class ATDFluidLiveActor;
class UTDFluidLiveComponent;

UCLASS(ClassGroup = (FluidFX), meta = (BlueprintSpawnableComponent))
class TDGAME_API UTDFluidRuntimeControlComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTDFluidRuntimeControlComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "FluidControl", meta = (ClampMin = "0.0"))
	float DissipationScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "FluidControl", meta = (ClampMin = "0.0"))
	float InputStrengthScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "FluidControl", meta = (ClampMin = "0.0"))
	float VorticityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "FluidControl", meta = (ClampMin = "0.0"))
	float FlowNoiseScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "FluidControl", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WaterDisplayAlpha = 1.0f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void FadeOut(float Seconds = -1.0f);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void FadeIn();

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void CancelFadeOut();

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	bool IsFadingOut() const { return bIsFadingOut; }

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	int32 GetAppliedConfigCount() const { return AppliedConfigCount; }

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	static UTDFluidRuntimeControlComponent* FindOrAddRuntimeControl(ATDFluidLiveActor* LiveActor);

	const FTDFluidSimulationConfig& GetBaseConfig() const { return BaseConfig; }

private:
	UTDFluidLiveComponent* ResolveLiveComponent() const;
	void RefreshBaseConfig(const UTDFluidLiveComponent& LiveComponent);
	float AdvanceFadeFraction(float DeltaTime);
	void ApplyComposedConfig(UTDFluidLiveComponent& LiveComponent, float FadeFraction);
	void FinishFadeOut(UTDFluidLiveComponent& LiveComponent);
	void RestoreFromFade();

	FTDFluidSimulationConfig BaseConfig;
	FTDFluidSimulationConfig LastComposedConfig;
	FTDFluidSimulationConfig ReadBackConfig;
	bool bHasAppliedConfig = false;
	bool bIsFadingOut = false;
	bool bIsAwaitingFadeSleep = false;
	bool bHasSavedActivationFlag = false;
	bool bSavedShouldUseActivationVolume = false;
	float FadeElapsedSeconds = 0.0f;
	float FadeDurationSeconds = 1.0f;
	int32 AppliedConfigCount = 0;
};
