#include "Combat/Feedback/TDHitFeelRules.h"

#include "Combat/Feedback/TDCombatFeedbackSettings.h"
#include "Combat/Feedback/TDCombatFeedbackSubsystem.h"

float FTDTraumaWindow::AddHit(const double Now, const FObjectKey AttackerKey, const int32 StageIndex, const ETDHitFeelTier Tier, const ETDKillRank KillRank, const float AttackerStopSeconds, const bool bIsSlowMotionActive, const float CurrentChannelTrauma)
{
	if (!IsSameSwing(Now, AttackerKey, StageIndex))
	{
		*this = FTDTraumaWindow();
		bHasSwing = true;
		SwingAttackerKey = AttackerKey;
		SwingStageIndex = StageIndex;
		SwingStartTime = Now;
	}

	++TargetCount;
	StrongestHitTrauma = FMath::Max(StrongestHitTrauma, UTDCombatFeedbackSubsystem::ComputeDealtTrauma(Tier, 0, ETDKillRank::None));
	StrongestAttackerStopSeconds = FMath::Max(StrongestAttackerStopSeconds, AttackerStopSeconds);
	if (KillRank != ETDKillRank::None)
	{
		++KillCount;
		StrongestKillRank = static_cast<ETDKillRank>(FMath::Max(static_cast<uint8>(StrongestKillRank), static_cast<uint8>(KillRank)));
	}

	const float SwingTrauma = ComputeSwingTrauma(bIsSlowMotionActive);
	return FMath::Max(SwingTrauma - FMath::Max(CurrentChannelTrauma, 0.f), 0.f);
}

float FTDTraumaWindow::MergeRumble(const float Intensity)
{
	if (Intensity <= PlayedRumbleIntensity)
	{
		return 0.f;
	}
	PlayedRumbleIntensity = Intensity;
	return Intensity;
}

bool FTDTraumaWindow::IsSameSwing(const double Now, const FObjectKey AttackerKey, const int32 StageIndex) const
{
	if (!bHasSwing || SwingAttackerKey != AttackerKey || SwingStageIndex != StageIndex)
	{
		return false;
	}
	return Now - SwingStartTime < GetDefault<UTDCombatFeedbackSettings>()->DealtSwingSeconds;
}

float FTDTraumaWindow::ComputeSwingTrauma(const bool bIsSlowMotionActive) const
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const int32 ExtraTargetCount = FMath::Max(TargetCount - 1, 0);
	const float ExtraTargetTrauma = FMath::Min(FeedbackSettings.DealtExtraTargetTrauma * ExtraTargetCount, FeedbackSettings.MaxDealtExtraTargetTrauma);
	const float HitTrauma = TargetCount > 0 ? FMath::Min(StrongestHitTrauma + ExtraTargetTrauma, FeedbackSettings.DealtChannelTraumaCap) : 0.f;
	const float KillTrauma = KillCount > 0 ? UTDCombatFeedbackSubsystem::ComputeDealtTrauma(ETDHitFeelTier::Light, KillCount, StrongestKillRank) : 0.f;
	const float SwingTrauma = FMath::Max(HitTrauma, KillTrauma);
	return bIsSlowMotionActive ? FMath::Min(SwingTrauma, FeedbackSettings.SlowMotionDealtTraumaCap) : SwingTrauma;
}

float FTDTraumaWindow::ComputeChannelCap() const
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	switch (StrongestKillRank)
	{
	case ETDKillRank::Boss:
		return FeedbackSettings.BossKillShakeTraumaCap;
	case ETDKillRank::Elite:
		return FeedbackSettings.EliteKillTraumaCap;
	case ETDKillRank::Normal:
		return FeedbackSettings.KillTraumaCap;
	default:
		return FeedbackSettings.DealtChannelTraumaCap;
	}
}

float FTDTraumaWindow::ComputeDesiredAttackerStopSeconds() const
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const float CrowdScale = TargetCount >= FeedbackSettings.CrowdHitStopTargetCount ? FeedbackSettings.CrowdHitStopScale : 1.f;
	return StrongestAttackerStopSeconds * CrowdScale;
}

FTDReceivedTraumaResult FTDReceivedHitAccumulator::AddHit(const double Now, const float DamageRatio, const bool bIsStrongSource, const bool bIsDeath, const float CurrentTrauma, const FVector& HitDirection)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	FTDReceivedTraumaResult Result;
	if (!bIsDeath && bIsStrongSource && DamageRatio < FeedbackSettings.ReceivedStrongSourceMinRatio)
	{
		return Result;
	}

	RecentHits.RemoveAll([Now, &FeedbackSettings](const TPair<double, float>& Hit)
	{
		return Now - Hit.Key >= FeedbackSettings.ReceivedWindowSeconds;
	});
	RecentHits.Emplace(Now, FMath::Max(DamageRatio, 0.f));
	const float AccumulatedRatio = ComputeAccumulatedRatio();
	const bool bIsRefractory = bHasShaken && Now - LastShakeTime < FeedbackSettings.ReceivedRefractorySeconds;

	float TargetTrauma = 0.f;
	if (bIsRefractory)
	{
		TargetTrauma = FMath::Max(CurrentTrauma, UTDCombatFeedbackSubsystem::ComputeReceivedTrauma(AccumulatedRatio, bIsDeath)) + FeedbackSettings.ReceivedRefractoryBonusTrauma;
	}
	else if (bIsDeath || AccumulatedRatio >= FeedbackSettings.ReceivedMinAccumulatedRatio)
	{
		TargetTrauma = UTDCombatFeedbackSubsystem::ComputeReceivedTrauma(AccumulatedRatio, bIsDeath);
		Result.bStartsShake = true;
	}
	else
	{
		return Result;
	}

	RecentInjections.RemoveAll([Now, &FeedbackSettings](const TPair<double, float>& Injection)
	{
		return Now - Injection.Key >= FeedbackSettings.ReceivedInjectionWindowSeconds;
	});
	const float RemainingInjection = FMath::Max(FeedbackSettings.MaxReceivedInjectionTrauma - ComputeRecentInjection(), 0.f);
	Result.TraumaIncrement = FMath::Min(FMath::Max(TargetTrauma - CurrentTrauma, 0.f), RemainingInjection);
	if (Result.TraumaIncrement <= KINDA_SMALL_NUMBER)
	{
		return FTDReceivedTraumaResult();
	}

	RecentInjections.Emplace(Now, Result.TraumaIncrement);
	bHasShaken = true;
	LastShakeTime = Now;
	if (!Result.bStartsShake)
	{
		return Result;
	}

	const FVector Direction = HitDirection.GetSafeNormal2D();
	const double SecondsSinceKick = Now - LastKickTime;
	const bool bIsTooSoon = bHasKicked && SecondsSinceKick < FeedbackSettings.KickMinIntervalSeconds;
	const bool bIsReversal = bHasKicked && SecondsSinceKick < FeedbackSettings.KickOppositeBlockSeconds && (Direction | LastKickDirection) < 0.f;
	if (bIsTooSoon || bIsReversal)
	{
		return Result;
	}

	Result.bShouldKick = true;
	bHasKicked = true;
	LastKickTime = Now;
	LastKickDirection = Direction;
	return Result;
}

float FTDReceivedHitAccumulator::ComputeAccumulatedRatio() const
{
	float AccumulatedRatio = 0.f;
	for (const TPair<double, float>& Hit : RecentHits)
	{
		AccumulatedRatio += Hit.Value;
	}
	return AccumulatedRatio;
}

float FTDReceivedHitAccumulator::ComputeRecentInjection() const
{
	float InjectedTrauma = 0.f;
	for (const TPair<double, float>& Injection : RecentInjections)
	{
		InjectedTrauma += Injection.Value;
	}
	return InjectedTrauma;
}

bool FTDSlowMotionArbiter::Request(const double Now, const float Scale, const float RealSeconds, const bool bIgnoresCooldown, const bool bStartsCooldown)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	if (!FeedbackSettings.IsSlowMotionEnabled() || Scale <= 0.f || Scale >= 1.f || RealSeconds <= 0.f)
	{
		return false;
	}

	RemoveExpired(Now);
	if (Requests.IsEmpty())
	{
		if (!bIgnoresCooldown && Now < CooldownEndTime)
		{
			return false;
		}
		SessionStartTime = Now;
	}

	const double EndTime = FMath::Min(Now + RealSeconds, SessionStartTime + FeedbackSettings.SlowMotionMaxRealSeconds);
	if (EndTime <= Now)
	{
		return false;
	}
	if (bStartsCooldown)
	{
		CooldownEndTime = FMath::Max(CooldownEndTime, Now + FeedbackSettings.SlowMotionCooldownSeconds);
	}

	FTDSlowMotionRequest& NewRequest = Requests.AddDefaulted_GetRef();
	NewRequest.Scale = Scale;
	NewRequest.EndTime = EndTime;
	return true;
}

bool FTDSlowMotionArbiter::RemoveExpired(const double Now)
{
	return Requests.RemoveAll([Now](const FTDSlowMotionRequest& Request)
	{
		return Request.EndTime <= Now;
	}) > 0;
}

float FTDSlowMotionArbiter::GetScale() const
{
	float Scale = 1.f;
	for (const FTDSlowMotionRequest& Request : Requests)
	{
		Scale = FMath::Min(Scale, Request.Scale);
	}
	return Scale;
}

void FTDSlowMotionArbiter::ClearRequests()
{
	Requests.Reset();
}

bool FTDFlashRateLimiter::CanStart(const double Now) const
{
	return !bHasStarted || Now - LastStartTime >= GetDefault<UTDCombatFeedbackSettings>()->FlashMinIntervalSeconds;
}

void FTDFlashRateLimiter::MarkStarted(const double Now)
{
	bHasStarted = true;
	LastStartTime = Now;
}

float FTDSoundBurstLimiter::TryPlay(const double Now)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	if (Count == 0 || Now - WindowStartTime >= FeedbackSettings.SameSoundBurstSeconds)
	{
		WindowStartTime = Now;
		Count = 0;
	}
	if (Count >= FeedbackSettings.SameSoundBurstMaxCount)
	{
		return 0.f;
	}
	++Count;
	return 1.f / FMath::Sqrt(static_cast<float>(Count));
}
