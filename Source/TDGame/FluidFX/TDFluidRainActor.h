#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TDFluidRainActor.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
struct FStreamableHandle;

UCLASS()
class TDGAME_API ATDFluidRainActor : public AActor
{
	GENERATED_BODY()

public:
	ATDFluidRainActor();

	UPROPERTY(VisibleAnywhere, Category = "Rain")
	TObjectPtr<UNiagaraComponent> RainVisual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain")
	TSoftObjectPtr<UNiagaraSystem> RainSystem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RainIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rain")
	bool bShouldFollowViewTarget = true;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Rain")
	void SetRainIntensity(float InRainIntensity);

	UFUNCTION(BlueprintPure, Category = "Rain")
	float GetRainIntensity() const { return RainIntensity; }

private:
	void RequestRainSystem();
	void ApplyLoadedRainSystem();
	void FollowViewTarget();
	void PushRainIntensity();

	TSharedPtr<FStreamableHandle> RainSystemLoadHandle;
};
