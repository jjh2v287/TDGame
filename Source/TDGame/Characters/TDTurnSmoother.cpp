#include "Characters/TDTurnSmoother.h"

namespace TDTurnSmoothing
{
	float LimitRateChange(const float PreviousRate, const float ProposedRate, const float MaxSpeedUp, const float MaxSlowDown, const float StepSeconds)
	{
		if (PreviousRate * ProposedRate > 0.f)
		{
			const float PreviousSpeed = FMath::Abs(PreviousRate);
			const float ProposedSpeed = FMath::Abs(ProposedRate);
			const float LimitedSpeed = ProposedSpeed > PreviousSpeed
				? FMath::Min(ProposedSpeed, PreviousSpeed + MaxSpeedUp * StepSeconds)
				: FMath::Max(ProposedSpeed, PreviousSpeed - MaxSlowDown * StepSeconds);
			return FMath::Sign(ProposedRate) * LimitedSpeed;
		}
		const float StopSeconds = FMath::Abs(PreviousRate) / MaxSlowDown;
		if (StopSeconds >= StepSeconds)
		{
			return PreviousRate - FMath::Sign(PreviousRate) * MaxSlowDown * StepSeconds;
		}
		return FMath::Sign(ProposedRate) * FMath::Min(FMath::Abs(ProposedRate), MaxSpeedUp * (StepSeconds - StopSeconds));
	}
}

void FTDTurnSmoother::Reset(const float NewYaw)
{
	Yaw = FRotator::NormalizeAxis(NewYaw);
	YawRate = 0.f;
	bIsInitialized = true;
}

void FTDTurnSmoother::SetYaw(const float NewYaw)
{
	Yaw = FRotator::NormalizeAxis(NewYaw);
	bIsInitialized = true;
}

float FTDTurnSmoother::Advance(const float TargetYaw, const float DeltaSeconds, const FTDTurnSmoothingSettings& Settings)
{
	if (!bIsInitialized || Settings.SmoothingSeconds <= UE_KINDA_SMALL_NUMBER)
	{
		Reset(TargetYaw);
		return Yaw;
	}
	if (DeltaSeconds > 0.f)
	{
		const int32 StepCount = FMath::Clamp(FMath::CeilToInt(DeltaSeconds / FMath::Max(Settings.MaxStepSeconds, UE_KINDA_SMALL_NUMBER)), 1, FMath::Max(Settings.MaxStepCount, 1));
		const float StepSeconds = DeltaSeconds / StepCount;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			AdvanceStep(TargetYaw, StepSeconds, Settings);
		}
	}
	return Yaw;
}

float FTDTurnSmoother::ComputeStopYaw(const FTDTurnSmoothingSettings& Settings) const
{
	return FRotator::NormalizeAxis(Yaw + YawRate * Settings.SmoothingSeconds * 0.5f);
}

void FTDTurnSmoother::AdvanceStep(const float TargetYaw, const float StepSeconds, const FTDTurnSmoothingSettings& Settings)
{
	const float Offset = FRotator::NormalizeAxis(Yaw - TargetYaw);
	const float Frequency = 2.f / Settings.SmoothingSeconds;
	const float Decay = FMath::Exp(-Frequency * StepSeconds);
	const float Drive = YawRate + Frequency * Offset;
	const float SpringOffset = (Offset + Drive * StepSeconds) * Decay;
	const float SpringRate = (YawRate - Drive * Frequency * StepSeconds) * Decay;
	const float MaxSlowDown = FMath::Max(Frequency * Settings.MaxDegreesPerSecond, UE_KINDA_SMALL_NUMBER);
	const float ChangeLimitedRate = TDTurnSmoothing::LimitRateChange(YawRate, SpringRate, Settings.MaxSpeedUpDegreesPerSecondSquared, MaxSlowDown, StepSeconds);
	const float LimitedRate = FMath::Clamp(ChangeLimitedRate, -Settings.MaxDegreesPerSecond, Settings.MaxDegreesPerSecond);
	const float NewOffset = LimitedRate == SpringRate ? SpringOffset : Offset + 0.5f * (YawRate + LimitedRate) * StepSeconds;
	YawRate = LimitedRate;
	Yaw = FRotator::NormalizeAxis(TargetYaw + NewOffset);
}
