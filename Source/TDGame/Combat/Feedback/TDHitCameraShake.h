#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "TDHitCameraShake.generated.h"

namespace TDHitCameraShakeMath
{
	TDGAME_API float ComputeShakeAmount(float Trauma);
}

UCLASS()
class TDGAME_API UTDTraumaCameraShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

public:
	UTDTraumaCameraShakePattern(const FObjectInitializer& ObjectInitializer);

	void AddTrauma(float Amount);
	void DecayTrauma(float DeltaSeconds);
	float GetTrauma() const { return Trauma; }

	UPROPERTY(EditAnywhere, Category="Trauma", meta=(ClampMin="0.0"))
	float TraumaDecayPerSecond = 2.2f;

	UPROPERTY(EditAnywhere, Category="Trauma")
	FVector MaxLocationOffset = FVector(0.f, 28.f, 20.f);

	UPROPERTY(EditAnywhere, Category="Trauma")
	FRotator MaxRotationOffset = FRotator(1.2f, 1.2f, 0.8f);

	UPROPERTY(EditAnywhere, Category="Trauma", meta=(ClampMin="0.0"))
	float NoiseFrequency = 18.f;

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override;
	virtual void StopShakePatternImpl(const FCameraShakePatternStopParams& Params) override;
	virtual void TeardownShakePatternImpl() override;

	float SampleNoise(int32 Channel) const;

	float Trauma = 0.f;
	float NoiseTime = 0.f;
};

UCLASS()
class TDGAME_API UTDHitCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UTDHitCameraShake(const FObjectInitializer& ObjectInitializer);
};
