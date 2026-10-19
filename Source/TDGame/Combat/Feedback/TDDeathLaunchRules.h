#pragma once

#include "CoreMinimal.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Combat/Feedback/TDHitFeelRules.h"

enum class ETDDeathPresentation : uint8
{
	Ragdoll,
	ClipThenRagdoll,
	ClipOnly
};

struct FTDDeathPresentationInput
{
	bool bIsBodyAirborne = false;
	bool bHasRagdollPhysicsAsset = false;
	bool bShouldPlayDeathClipBeforeRagdoll = false;
	bool bHasDeathClip = false;
	bool bShouldRagdollAfterDeathClip = false;
	bool bShouldUseRagdollWithoutDeathClip = false;
};

struct FTDDeathLaunchInput
{
	ETDDeathPresentation Presentation = ETDDeathPresentation::Ragdoll;
	ETDDamageDelivery Delivery = ETDDamageDelivery::Direct;
	ETDKillRank KillRank = ETDKillRank::Normal;
	FVector HitDirection = FVector::ZeroVector;
	FVector SwingDirection = FVector::ZeroVector;
	FVector ActorForward = FVector::ForwardVector;
	float DamageRatio = 0.f;
	float UnclampedDamage = 0.f;
	float MaxHealth = 0.f;
	float CapsuleHalfHeight = 90.f;
	float ActorHeight = 0.f;
	float CenterOfMassHeight = 0.f;
	float GroundHeight = 0.f;
	float RestHeightRatio = 0.f;
	uint32 VariationSeed = 0;
	bool bHasGroundHeight = false;
	bool bWasCritical = false;
	bool bIsComboFinisher = false;
	bool bIsPlayer = false;
	bool bHasHorizontalBodyPlan = false;
	bool bStartsAtRest = false;
};

struct FTDDeathLaunch
{
	FVector LinearVelocity = FVector::ZeroVector;
	FVector AngularVelocityRadians = FVector::ZeroVector;
	FVector ExtraGravity = FVector::ZeroVector;
	FVector PlanarDirection = FVector::ZeroVector;
	float ExpectedFallSeconds = 0.f;
	float GroundHeight = 0.f;
	float RestHeight = 0.f;
	float DropHeight = 0.f;
	bool bWatchesLanding = false;
};

struct FTDDeathLaunchVariation
{
	float DirectionDegrees = 0.f;
	float DistanceScale = 1.f;
	float SpinScale = 1.f;
	float AxisTiltDegrees = 0.f;
	float YawDegreesPerSecond = 0.f;
	float SideSign = 1.f;
};

struct FTDKillPoseHoldInput
{
	ETDHitFeelTier Tier = ETDHitFeelTier::Light;
	ETDKillRank KillRank = ETDKillRank::Normal;
	float GroundAreaDistance = 0.f;
	int32 MultiKillIndex = 0;
	uint32 VariationSeed = 0;
	bool bWasCritical = false;
	bool bIsPlayer = false;
	bool bIsGroundAreaKill = false;
};

struct FTDSlowMotionStartInput
{
	double RequestedStartRealSeconds = 0.;
	double NowRealSeconds = 0.;
	double MaxRagdollDelaySeconds = 0.;
	TOptional<double> LatestRagdollEntryRealSeconds;
	bool bHasRagdollAwaitingEntry = false;
};

struct FTDRagdollDeferInput
{
	double NowRealSeconds = 0.;
	double ScheduledEntryRealSeconds = 0.;
	TOptional<double> NextSlowMotionStartRealSeconds;
	float DeferredRealSeconds = 0.f;
	float GlobalTimeDilation = 1.f;
	bool bIsSlowMotionActive = false;
};

enum class ETDDeathLandingState : uint8
{
	Falling,
	Landed,
	Overdue,
	Abandoned
};

struct FTDDeathLandingSample
{
	float VerticalSpeed = 0.f;
	float MinVerticalSpeed = 0.f;
	float CenterOfMassHeight = 0.f;
	float ElapsedSeconds = 0.f;
};

struct FTDCorpseLandSound
{
	float VolumeScale = 0.f;
	float PitchScale = 1.f;
};

namespace TDDeathLaunchRules
{
	inline constexpr float LaunchLinearDamping = 0.6f;
	inline constexpr float LaunchAngularDamping = 1.f;
	inline constexpr float BiteLinearDamping = 5.f;
	inline constexpr float BiteSeconds = 0.35f;
	inline constexpr float MaxLandingRollDegreesPerSecond = 200.f;
	inline constexpr float MaxLandingRollCapsuleHalfHeight = 70.f;
	inline constexpr float MinLandingRollTorsoMassRatio = 0.3f;

	TDGAME_API ETDDeathPresentation ResolveDeathPresentation(const FTDDeathPresentationInput& Input);
	TDGAME_API FTDDeathLaunch ComputeDeathLaunch(const FTDDeathLaunchInput& Input);
	TDGAME_API FTDDeathLaunchVariation MakeDeathLaunchVariation(uint32 Seed);
	TDGAME_API float ComputeKillPoseHoldRealSeconds(const FTDKillPoseHoldInput& Input);
	TDGAME_API float ComputeMultiKillStaggerSeconds(uint32 Seed, int32 MultiKillIndex);
	TDGAME_API double ResolveSlowMotionStartRealSeconds(const FTDSlowMotionStartInput& Input);
	TDGAME_API bool ShouldDeferRagdollStart(const FTDRagdollDeferInput& Input);
	TDGAME_API ETDDeathLandingState ResolveDeathLandingState(const FTDDeathLaunch& Launch, const FTDDeathLandingSample& Sample);
	TDGAME_API FTDCorpseLandSound ComputeCorpseLandSound(const FTDDeathLaunch& Launch, const FTDDeathLandingSample& Sample, float CapsuleHalfHeight);
	TDGAME_API bool ShouldAddCorpseLandTrauma(ETDKillRank KillRank, float CapsuleHalfHeight, float ViewTargetDistance);
	TDGAME_API float ComputeBiteAngularDamping(bool bHasHorizontalBodyPlan);
	TDGAME_API bool ShouldLimitLandingRoll(bool bHasHorizontalBodyPlan, float CapsuleHalfHeight, float TorsoMassRatio);
	TDGAME_API bool IsLandingRollTorsoBone(FName BoneName);
	TDGAME_API float ComputeLandingRollSpeedExcess(float RollRadiansPerSecond);
}
