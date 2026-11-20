#pragma once

#include "CoreMinimal.h"

struct FTDTurnSmoothingSettings
{
	float SmoothingSeconds = 0.06f;
	float MaxDegreesPerSecond = 540.f;
	float MaxSpeedUpDegreesPerSecondSquared = 8000.f;
	float MaxStepSeconds = 1.f / 240.f;
	int32 MaxStepCount = 64;
};

class TDGAME_API FTDTurnSmoother
{
public:
	void Reset(float NewYaw);
	void SetYaw(float NewYaw);
	float Advance(float TargetYaw, float DeltaSeconds, const FTDTurnSmoothingSettings& Settings);
	float ComputeStopYaw(const FTDTurnSmoothingSettings& Settings) const;
	bool IsInitialized() const { return bIsInitialized; }
	float GetYaw() const { return Yaw; }
	float GetYawRate() const { return YawRate; }

private:
	void AdvanceStep(float TargetYaw, float StepSeconds, const FTDTurnSmoothingSettings& Settings);

	float Yaw = 0.f;
	float YawRate = 0.f;
	bool bIsInitialized = false;
};
