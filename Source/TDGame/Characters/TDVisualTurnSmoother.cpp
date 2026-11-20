#include "Characters/TDVisualTurnSmoother.h"

namespace TDVisualTurnSmoothing
{
	float LimitSpeedUp(const float PreviousRate, const float ProposedRate, const float MaxSpeedUp, const float StepSeconds)
	{
		const bool bKeepsDirection = PreviousRate * ProposedRate > 0.f;
		const float StartSpeed = bKeepsDirection ? FMath::Abs(PreviousRate) : 0.f;
		const float ProposedSpeed = FMath::Abs(ProposedRate);
		if (ProposedSpeed <= StartSpeed)
		{
			return ProposedRate;
		}
		return FMath::Sign(ProposedRate) * FMath::Min(ProposedSpeed, StartSpeed + MaxSpeedUp * StepSeconds);
	}
}

void FTDVisualTurnSmoother::Reset(const float LogicalYaw)
{
	VisualYaw = FRotator::NormalizeAxis(LogicalYaw);
	YawRate = 0.f;
	bIsInitialized = true;
}

float FTDVisualTurnSmoother::Advance(const float LogicalYaw, const float DeltaSeconds, const FTDVisualTurnSettings& Settings)
{
	if (!bIsInitialized || Settings.SmoothingSeconds <= UE_KINDA_SMALL_NUMBER)
	{
		Reset(LogicalYaw);
		return 0.f;
	}
	if (DeltaSeconds > 0.f)
	{
		const int32 StepCount = FMath::Clamp(FMath::CeilToInt(DeltaSeconds / FMath::Max(Settings.MaxStepSeconds, UE_KINDA_SMALL_NUMBER)), 1, FMath::Max(Settings.MaxStepCount, 1));
		const float StepSeconds = DeltaSeconds / StepCount;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			AdvanceStep(LogicalYaw, StepSeconds, Settings);
		}
	}
	return FRotator::NormalizeAxis(VisualYaw - LogicalYaw);
}

void FTDVisualTurnSmoother::AdvanceStep(const float LogicalYaw, const float StepSeconds, const FTDVisualTurnSettings& Settings)
{
	const float Offset = FRotator::NormalizeAxis(VisualYaw - LogicalYaw);
	const float Frequency = 2.f / Settings.SmoothingSeconds;
	const float Decay = FMath::Exp(-Frequency * StepSeconds);
	const float Drive = YawRate + Frequency * Offset;
	const float SpringOffset = (Offset + Drive * StepSeconds) * Decay;
	const float SpringRate = (YawRate - Drive * Frequency * StepSeconds) * Decay;
	const float SpeedLimitedRate = TDVisualTurnSmoothing::LimitSpeedUp(YawRate, SpringRate, Settings.MaxSpeedUpDegreesPerSecondSquared, StepSeconds);
	const float LimitedRate = FMath::Clamp(SpeedLimitedRate, -Settings.MaxDegreesPerSecond, Settings.MaxDegreesPerSecond);
	const float NewOffset = LimitedRate == SpringRate ? SpringOffset : Offset + 0.5f * (YawRate + LimitedRate) * StepSeconds;
	YawRate = LimitedRate;
	VisualYaw = FRotator::NormalizeAxis(LogicalYaw + NewOffset);
}
