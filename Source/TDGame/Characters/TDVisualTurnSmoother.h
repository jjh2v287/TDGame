#pragma once

#include "CoreMinimal.h"

struct FTDVisualTurnSettings
{
	float SmoothingSeconds = 0.06f;
	float MaxDegreesPerSecond = 720.f;
	float MaxSpeedUpDegreesPerSecondSquared = 8000.f;
	float MaxStepSeconds = 1.f / 240.f;
	int32 MaxStepCount = 64;
};

class TDGAME_API FTDVisualTurnSmoother
{
public:
	void Reset(float LogicalYaw);
	float Advance(float LogicalYaw, float DeltaSeconds, const FTDVisualTurnSettings& Settings);

	bool IsInitialized() const { return bIsInitialized; }
	float GetVisualYaw() const { return VisualYaw; }
	float GetYawRate() const { return YawRate; }

private:
	void AdvanceStep(float LogicalYaw, float StepSeconds, const FTDVisualTurnSettings& Settings);

	float VisualYaw = 0.f;
	float YawRate = 0.f;
	bool bIsInitialized = false;
};
