#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"

enum class ETDHitFeelTier : uint8
{
	Light,
	Heavy
};

enum class ETDKillRank : uint8
{
	None,
	Normal,
	Elite,
	Boss
};

struct FTDReceivedTraumaResult
{
	float TraumaIncrement = 0.f;
	bool bStartsShake = false;
	bool bShouldKick = false;
};

struct FTDSlowMotionRequest
{
	float Scale = 1.f;
	double EndTime = 0.;
};

struct TDGAME_API FTDTraumaWindow
{
	float AddHit(double Now, FObjectKey AttackerKey, int32 StageIndex, ETDHitFeelTier Tier, ETDKillRank KillRank, float AttackerStopSeconds, bool bIsSlowMotionActive, float CurrentChannelTrauma);
	float MergeRumble(float Intensity);
	bool IsSameSwing(double Now, FObjectKey AttackerKey, int32 StageIndex) const;
	float ComputeSwingTrauma(bool bIsSlowMotionActive) const;
	float ComputeChannelCap() const;
	float ComputeDesiredAttackerStopSeconds() const;

	FObjectKey SwingAttackerKey;
	double SwingStartTime = 0.;
	double AttackerStopStartTime = 0.;
	int32 SwingStageIndex = INDEX_NONE;
	int32 TargetCount = 0;
	int32 KillCount = 0;
	ETDKillRank StrongestKillRank = ETDKillRank::None;
	float StrongestHitTrauma = 0.f;
	float StrongestAttackerStopSeconds = 0.f;
	float PlayedRumbleIntensity = 0.f;
	float GrantedAttackerStopSeconds = 0.f;
	bool bHasSwing = false;
};

struct TDGAME_API FTDReceivedHitAccumulator
{
	FTDReceivedTraumaResult AddHit(double Now, float DamageRatio, bool bIsStrongSource, bool bIsDeath, float CurrentTrauma, const FVector& HitDirection);
	float ComputeAccumulatedRatio() const;
	float ComputeRecentInjection() const;

	TArray<TPair<double, float>> RecentHits;
	TArray<TPair<double, float>> RecentInjections;
	FVector LastKickDirection = FVector::ZeroVector;
	double LastShakeTime = 0.;
	double LastKickTime = 0.;
	bool bHasShaken = false;
	bool bHasKicked = false;
};

struct TDGAME_API FTDSlowMotionArbiter
{
	bool Request(double Now, float Scale, float RealSeconds, bool bIgnoresCooldown, bool bStartsCooldown);
	bool RemoveExpired(double Now);
	float GetScale() const;
	bool IsActive() const { return !Requests.IsEmpty(); }
	void ClearRequests();

	TArray<FTDSlowMotionRequest> Requests;
	double SessionStartTime = 0.;
	double CooldownEndTime = 0.;
};

struct TDGAME_API FTDFlashRateLimiter
{
	bool CanStart(double Now) const;
	void MarkStarted(double Now);

	double LastStartTime = 0.;
	bool bHasStarted = false;
};

struct TDGAME_API FTDSoundBurstLimiter
{
	float TryPlay(double Now);

	double WindowStartTime = 0.;
	int32 Count = 0;
};
