#include "Combat/Feedback/TDDeathLaunchRules.h"

#include "Combat/Feedback/TDCombatFeedbackSettings.h"
#include "HAL/IConsoleManager.h"

namespace TDDeathLaunchRules
{
	namespace
	{
		TAutoConsoleVariable<int32> CVarDeathImmediateRagdoll(
			TEXT("TD.Death.ImmediateRagdoll"),
			1,
			TEXT("Starts the death ragdoll right after the kill pose hold instead of playing the death clip when set to 1."));

		TAutoConsoleVariable<float> CVarDeathSwingTurnScale(
			TEXT("TD.Death.SwingTurnScale"),
			0.5f,
			TEXT("Scales how far a melee kill turns the death launch from the hit direction toward the swing direction."));

		TAutoConsoleVariable<float> CVarDeathLaunchScale(
			TEXT("TD.Death.LaunchScale"),
			1.f,
			TEXT("Scales the airborne distance of death launches."));

		enum class ETDDeathVariationChannel : uint32
		{
			Direction,
			Distance,
			Spin,
			AxisTilt,
			YawSign,
			YawSpeed,
			Side,
			MultiKillStagger
		};

		constexpr float GravityAcceleration = 980.f;
		constexpr float ExtraGravityScale = 0.6f;
		constexpr float EffectiveGravityAcceleration = GravityAcceleration * (1.f + ExtraGravityScale);

		constexpr float MinPlanarSwingLength = 0.25f;
		constexpr float SwingTurnCapDegrees = 35.f;
		constexpr float FinisherSwingTurnRatio = 0.5f;
		constexpr float FinisherSwingTurnCapDegrees = 15.f;
		constexpr float MaxDeviationFromHitDegrees = 37.f;

		constexpr float LaunchUpSpeed = 100.f;
		constexpr float ChopUpSpeedScale = 0.3f;
		constexpr float ChopMinDownDegrees = 15.f;
		constexpr float MinDropHeight = 10.f;
		constexpr float UprightRestHeightRatio = 0.18f;
		constexpr float HorizontalRestHeightRatio = 0.45f;

		constexpr float BaseAirborneDistance = 110.f;
		constexpr float OverkillAirborneDistance = 60.f;
		constexpr float CriticalDistanceScale = 1.1f;
		constexpr float FinisherDistanceScale = 1.8f;
		constexpr float BossDistanceScale = 0.5f;
		constexpr float PlayerDistanceScale = 0.5f;
		constexpr float ReferenceHalfHeight = 90.f;
		constexpr float MinSizeScale = 0.55f;
		constexpr float MaxPlanarSpeed = 800.f;

		constexpr float BaseLandingDegrees = 75.f;
		constexpr float SpeedLandingDegrees = 20.f;
		constexpr float StatusLandingDegrees = 80.f;
		constexpr float HorizontalLandingDegrees = 70.f;
		constexpr float MaxHorizontalLandingDegrees = 95.f;
		constexpr float MinLandingDegrees = 70.f;
		constexpr float MaxLandingDegrees = 110.f;
		constexpr float MaxTipDegreesPerSecond = 720.f;
		constexpr float MaxTiltTipDegreesPerSecond = 360.f;
		constexpr float MaxVerticalSpinDegreesPerSecond = 300.f;
		constexpr float FrontHitSideThreshold = 0.3f;

		constexpr float DirectionVariationDegrees = 8.f;
		constexpr float DistanceVariation = 0.08f;
		constexpr float SpinVariation = 0.08f;
		constexpr float AxisTiltVariationDegrees = 25.f;
		constexpr float MinYawDegreesPerSecond = 30.f;
		constexpr float MaxYawDegreesPerSecond = 150.f;

		constexpr float MinKillPoseHoldSeconds = 0.07f;
		constexpr float MaxKillPoseHoldSeconds = 0.15f;
		constexpr float EliteKillPoseHoldBonusSeconds = 0.02f;
		constexpr float BossKillPoseHoldBonusSeconds = 0.03f;
		constexpr float GroundAreaWaveDistance = 400.f;
		constexpr float GroundAreaWaveSeconds = 0.1f;
		constexpr float MultiKillStaggerBaseSeconds = 0.02f;
		constexpr float MultiKillStaggerRangeSeconds = 0.05f;
		constexpr float PlayerDeathHoldSeconds = 0.1f;

		constexpr double SlowMotionRagdollLeadSeconds = 0.035;
		constexpr double SlowMotionLeadToleranceSeconds = 0.001;
		constexpr double ImminentSlowMotionSeconds = 0.05;
		constexpr float SlowTimeDilation = 0.95f;
		constexpr float MaxRagdollDeferSeconds = 1.5f;

		constexpr float LandingMinFallSpeed = -60.f;
		constexpr float LandingMinRecoverySpeed = 35.f;
		constexpr float LandingRecoveryRatio = 0.25f;
		constexpr float LandingHeightMargin = 25.f;
		constexpr float LandingFallbackSeconds = 0.25f;
		constexpr float LandingFallbackHeightMargin = 75.f;
		constexpr float LandingOverdueSeconds = 1.5f;
		constexpr float LandingAbandonSeconds = 3.f;

		constexpr float MinLandVolume = 0.3f;
		constexpr float SmallBodyHalfHeight = 60.f;
		constexpr float LargeBodyHalfHeight = 100.f;
		constexpr float SmallBodyLandVolume = 0.7f;
		constexpr float SmallBodyLandPitch = 1.1f;
		constexpr float LargeBodyLandVolume = 1.3f;
		constexpr float LargeBodyLandPitch = 0.85f;
		constexpr float LandingTraumaHalfHeight = 100.f;
		constexpr float MaxCorpseLandTraumaDistance = 1500.f;

		constexpr float UprightBiteAngularDamping = 3.f;
		constexpr float HorizontalBiteAngularDamping = 6.f;

		uint32 MixVariationHash(uint32 Hash)
		{
			Hash ^= Hash >> 16;
			Hash *= 0x85EBCA6Bu;
			Hash ^= Hash >> 13;
			Hash *= 0xC2B2AE35u;
			Hash ^= Hash >> 16;
			return Hash;
		}

		float MakeVariationUnit(const uint32 Seed, const ETDDeathVariationChannel Channel)
		{
			const uint32 Hash = MixVariationHash(MixVariationHash(Seed) + static_cast<uint32>(Channel) * 0x9E3779B9u);
			return static_cast<float>(Hash >> 8) / 16777216.f;
		}

		float MakeVariationSigned(const uint32 Seed, const ETDDeathVariationChannel Channel)
		{
			return MakeVariationUnit(Seed, Channel) * 2.f - 1.f;
		}

		float MakeVariationSign(const uint32 Seed, const ETDDeathVariationChannel Channel)
		{
			return MakeVariationUnit(Seed, Channel) < 0.5f ? -1.f : 1.f;
		}

		FVector RotatePlanar(const FVector& Direction, const float Degrees)
		{
			const float Radians = FMath::DegreesToRadians(Degrees);
			return (Direction * FMath::Cos(Radians) + (FVector::UpVector ^ Direction) * FMath::Sin(Radians)).GetSafeNormal2D();
		}

		float ComputeSignedPlanarDegrees(const FVector& From, const FVector& To)
		{
			return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2((From ^ To).Z, From | To)));
		}

		bool IsComboFinisher(const FTDDeathLaunchInput& Input)
		{
			return Input.bIsComboFinisher && Input.Delivery == ETDDamageDelivery::Melee;
		}

		float ComputeSwingTurnDegrees(const float SwingDegrees, const bool bIsFinisher)
		{
			const float TurnScale = FMath::Max(CVarDeathSwingTurnScale.GetValueOnGameThread(), 0.f);
			const float TurnRatio = bIsFinisher ? TurnScale * FinisherSwingTurnRatio : TurnScale;
			const float TurnCapDegrees = bIsFinisher ? FinisherSwingTurnCapDegrees : SwingTurnCapDegrees;
			return FMath::Sign(SwingDegrees) * FMath::Min(TurnRatio * FMath::Abs(SwingDegrees), TurnCapDegrees);
		}

		FVector ComputeLaunchPlanarDirection(const FTDDeathLaunchInput& Input, const FVector& HitPlanar, const FVector& Swing, const FTDDeathLaunchVariation& Variation)
		{
			const bool bHasPlanarSwing = Swing.Size2D() >= MinPlanarSwingLength;
			if (Input.Delivery == ETDDamageDelivery::Spell && bHasPlanarSwing)
			{
				return Swing.GetSafeNormal2D();
			}

			float TurnDegrees = 0.f;
			if (Input.Delivery == ETDDamageDelivery::Melee && bHasPlanarSwing)
			{
				TurnDegrees = ComputeSwingTurnDegrees(ComputeSignedPlanarDegrees(HitPlanar, Swing.GetSafeNormal2D()), IsComboFinisher(Input));
			}
			const float DeviationDegrees = FMath::Clamp(TurnDegrees + Variation.DirectionDegrees, -MaxDeviationFromHitDegrees, MaxDeviationFromHitDegrees);
			return RotatePlanar(HitPlanar, DeviationDegrees);
		}

		float ComputeLaunchUpSpeed(const FTDDeathLaunchInput& Input, const FVector& Swing)
		{
			if (Input.Delivery == ETDDamageDelivery::Status)
			{
				return 0.f;
			}
			const bool bIsDownwardChop = Swing.Z < -FMath::Sin(FMath::DegreesToRadians(ChopMinDownDegrees));
			return bIsDownwardChop ? LaunchUpSpeed * ChopUpSpeedScale : LaunchUpSpeed;
		}

		float ResolveRestHeightRatio(const FTDDeathLaunchInput& Input)
		{
			if (Input.RestHeightRatio > 0.f)
			{
				return Input.RestHeightRatio;
			}
			return Input.bHasHorizontalBodyPlan ? HorizontalRestHeightRatio : UprightRestHeightRatio;
		}

		float ComputeFallSeconds(const float UpSpeed, const float DropHeight)
		{
			return (UpSpeed + FMath::Sqrt(UpSpeed * UpSpeed + 2.f * EffectiveGravityAcceleration * DropHeight)) / EffectiveGravityAcceleration;
		}

		float ComputeOverkillRatio(const FTDDeathLaunchInput& Input)
		{
			const float UnclampedRatio = Input.MaxHealth > 0.f ? Input.UnclampedDamage / Input.MaxHealth : 0.f;
			return FMath::Clamp(FMath::Max(Input.DamageRatio, UnclampedRatio), 0.f, 1.f);
		}

		float ComputeAirborneDistance(const FTDDeathLaunchInput& Input, const FTDDeathLaunchVariation& Variation)
		{
			const float CriticalScale = Input.bWasCritical ? CriticalDistanceScale : 1.f;
			const float FinisherScale = IsComboFinisher(Input) ? FinisherDistanceScale : 1.f;
			const float RankScale = Input.KillRank == ETDKillRank::Boss ? BossDistanceScale : 1.f;
			const float SizeScale = FMath::Clamp(ReferenceHalfHeight / FMath::Max(Input.CapsuleHalfHeight, 1.f), MinSizeScale, 1.f);
			const float PlayerScale = Input.bIsPlayer ? PlayerDistanceScale : 1.f;
			const float LaunchScale = FMath::Max(CVarDeathLaunchScale.GetValueOnGameThread(), 0.f);
			const float BaseDistance = BaseAirborneDistance + OverkillAirborneDistance * ComputeOverkillRatio(Input);
			return BaseDistance * CriticalScale * FinisherScale * RankScale * SizeScale * PlayerScale * Variation.DistanceScale * LaunchScale;
		}

		float ComputePlanarSpeed(const float AirborneDistance, const float FallSeconds)
		{
			const float Coverage = 1.f - FMath::Exp(-LaunchLinearDamping * FallSeconds);
			if (Coverage <= UE_KINDA_SMALL_NUMBER)
			{
				return 0.f;
			}
			return FMath::Min(MaxPlanarSpeed, AirborneDistance * LaunchLinearDamping / Coverage);
		}

		float ComputeTipDegreesPerSecond(const float LandingDegrees, const float FallSeconds)
		{
			const float Coverage = 1.f - FMath::Exp(-LaunchAngularDamping * FallSeconds);
			if (Coverage <= UE_KINDA_SMALL_NUMBER)
			{
				return MaxTipDegreesPerSecond;
			}
			return FMath::Min(MaxTipDegreesPerSecond, LandingDegrees * LaunchAngularDamping / Coverage);
		}

		float ComputeTargetLandingDegrees(const FTDDeathLaunchInput& Input, const float PlanarSpeed)
		{
			if (Input.Delivery == ETDDamageDelivery::Status)
			{
				return StatusLandingDegrees;
			}
			if (Input.bHasHorizontalBodyPlan)
			{
				return HorizontalLandingDegrees;
			}
			return BaseLandingDegrees + SpeedLandingDegrees * (PlanarSpeed / MaxPlanarSpeed);
		}

		float ComputeVariedLandingDegrees(const FTDDeathLaunchInput& Input, const float TargetLandingDegrees, const FTDDeathLaunchVariation& Variation)
		{
			const float VariedLandingDegrees = FMath::Clamp(TargetLandingDegrees * Variation.SpinScale, MinLandingDegrees, MaxLandingDegrees);
			return Input.bHasHorizontalBodyPlan ? FMath::Min(VariedLandingDegrees, MaxHorizontalLandingDegrees) : VariedLandingDegrees;
		}

		FVector ComputeTipAxis(const FTDDeathLaunchInput& Input, const FVector& Forward, const FVector& FallDirection, const FTDDeathLaunchVariation& Variation)
		{
			if (!Input.bHasHorizontalBodyPlan)
			{
				return (FVector::UpVector ^ FallDirection).GetSafeNormal();
			}
			const float SideAlignment = static_cast<float>((Forward ^ FVector::UpVector) | FallDirection);
			const float SideSign = FMath::Abs(SideAlignment) < FrontHitSideThreshold ? Variation.SideSign : FMath::Sign(SideAlignment);
			return Forward * SideSign;
		}
	}

	ETDDeathPresentation ResolveDeathPresentation(const FTDDeathPresentationInput& Input)
	{
		if (Input.bIsBodyAirborne)
		{
			return ETDDeathPresentation::Ragdoll;
		}
		if (!Input.bHasRagdollPhysicsAsset)
		{
			return ETDDeathPresentation::ClipOnly;
		}
		const bool bIsImmediateRagdollEnabled = CVarDeathImmediateRagdoll.GetValueOnGameThread() != 0;
		if (bIsImmediateRagdollEnabled && !Input.bShouldPlayDeathClipBeforeRagdoll)
		{
			return ETDDeathPresentation::Ragdoll;
		}
		if (Input.bHasDeathClip)
		{
			return Input.bShouldRagdollAfterDeathClip ? ETDDeathPresentation::ClipThenRagdoll : ETDDeathPresentation::ClipOnly;
		}
		return Input.bShouldUseRagdollWithoutDeathClip ? ETDDeathPresentation::Ragdoll : ETDDeathPresentation::ClipOnly;
	}

	FTDDeathLaunch ComputeDeathLaunch(const FTDDeathLaunchInput& Input)
	{
		if (Input.Presentation != ETDDeathPresentation::Ragdoll || Input.bStartsAtRest)
		{
			return FTDDeathLaunch();
		}

		const FTDDeathLaunchVariation Variation = MakeDeathLaunchVariation(Input.VariationSeed);
		const FVector Forward = Input.ActorForward.GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
		const FVector HitPlanar = Input.HitDirection.GetSafeNormal2D(UE_SMALL_NUMBER, -Forward);
		const FVector Swing = Input.SwingDirection.GetSafeNormal();
		const bool bIsStatusKill = Input.Delivery == ETDDamageDelivery::Status;
		const FVector FallDirection = bIsStatusKill ? Forward : ComputeLaunchPlanarDirection(Input, HitPlanar, Swing, Variation);

		FTDDeathLaunch Launch;
		Launch.bWatchesLanding = true;
		Launch.ExtraGravity = FVector(0.f, 0.f, -GravityAcceleration * ExtraGravityScale);
		Launch.PlanarDirection = FallDirection;
		Launch.RestHeight = Input.CapsuleHalfHeight * ResolveRestHeightRatio(Input);
		Launch.GroundHeight = Input.bHasGroundHeight ? Input.GroundHeight : Input.ActorHeight - Input.CapsuleHalfHeight;
		Launch.DropHeight = FMath::Max(MinDropHeight, Input.CenterOfMassHeight - Launch.GroundHeight - Launch.RestHeight);

		const float UpSpeed = ComputeLaunchUpSpeed(Input, Swing);
		Launch.ExpectedFallSeconds = ComputeFallSeconds(UpSpeed, Launch.DropHeight);
		const float PlanarSpeed = bIsStatusKill ? 0.f : ComputePlanarSpeed(ComputeAirborneDistance(Input, Variation), Launch.ExpectedFallSeconds);
		Launch.LinearVelocity = FallDirection * PlanarSpeed + FVector::UpVector * UpSpeed;

		const float VariedLandingDegrees = ComputeVariedLandingDegrees(Input, ComputeTargetLandingDegrees(Input, PlanarSpeed), Variation);
		const float TipDegreesPerSecond = ComputeTipDegreesPerSecond(VariedLandingDegrees, Launch.ExpectedFallSeconds);
		const float TiltDegreesPerSecond = FMath::Min(TipDegreesPerSecond, MaxTiltTipDegreesPerSecond) * FMath::Tan(FMath::DegreesToRadians(Variation.AxisTiltDegrees));
		const float VerticalDegreesPerSecond = FMath::Clamp(TiltDegreesPerSecond + Variation.YawDegreesPerSecond, -MaxVerticalSpinDegreesPerSecond, MaxVerticalSpinDegreesPerSecond);
		const FVector TipAxis = ComputeTipAxis(Input, Forward, FallDirection, Variation);
		Launch.AngularVelocityRadians = TipAxis * FMath::DegreesToRadians(TipDegreesPerSecond) + FVector::UpVector * FMath::DegreesToRadians(VerticalDegreesPerSecond);
		return Launch;
	}

	FTDDeathLaunchVariation MakeDeathLaunchVariation(const uint32 Seed)
	{
		FTDDeathLaunchVariation Variation;
		Variation.DirectionDegrees = DirectionVariationDegrees * MakeVariationSigned(Seed, ETDDeathVariationChannel::Direction);
		Variation.DistanceScale = 1.f + DistanceVariation * MakeVariationSigned(Seed, ETDDeathVariationChannel::Distance);
		Variation.SpinScale = 1.f + SpinVariation * MakeVariationSigned(Seed, ETDDeathVariationChannel::Spin);
		Variation.AxisTiltDegrees = AxisTiltVariationDegrees * MakeVariationSigned(Seed, ETDDeathVariationChannel::AxisTilt);
		const float YawSpeed = FMath::Lerp(MinYawDegreesPerSecond, MaxYawDegreesPerSecond, MakeVariationUnit(Seed, ETDDeathVariationChannel::YawSpeed));
		Variation.YawDegreesPerSecond = MakeVariationSign(Seed, ETDDeathVariationChannel::YawSign) * YawSpeed;
		Variation.SideSign = MakeVariationSign(Seed, ETDDeathVariationChannel::Side);
		return Variation;
	}

	float ComputeKillPoseHoldRealSeconds(const FTDKillPoseHoldInput& Input)
	{
		if (Input.bIsPlayer)
		{
			return PlayerDeathHoldSeconds;
		}

		const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
		const float TierSeconds = Input.Tier == ETDHitFeelTier::Heavy ? FeedbackSettings.HeavyHitStopSeconds : FeedbackSettings.LightHitStopSeconds;
		const float CriticalSeconds = Input.bWasCritical ? FeedbackSettings.CriticalHitStopBonusSeconds : 0.f;
		float RankSeconds = 0.f;
		if (Input.KillRank == ETDKillRank::Elite)
		{
			RankSeconds = EliteKillPoseHoldBonusSeconds;
		}
		if (Input.KillRank == ETDKillRank::Boss)
		{
			RankSeconds = BossKillPoseHoldBonusSeconds;
		}
		const float HoldSeconds = FMath::Clamp(TierSeconds + CriticalSeconds + RankSeconds, MinKillPoseHoldSeconds, MaxKillPoseHoldSeconds);
		if (Input.bIsGroundAreaKill)
		{
			return HoldSeconds + FMath::Clamp(Input.GroundAreaDistance / GroundAreaWaveDistance, 0.f, 1.f) * GroundAreaWaveSeconds;
		}
		return HoldSeconds + ComputeMultiKillStaggerSeconds(Input.VariationSeed, Input.MultiKillIndex);
	}

	float ComputeMultiKillStaggerSeconds(const uint32 Seed, const int32 MultiKillIndex)
	{
		if (MultiKillIndex <= 0)
		{
			return 0.f;
		}
		return MultiKillStaggerBaseSeconds + MultiKillStaggerRangeSeconds * MakeVariationUnit(Seed, ETDDeathVariationChannel::MultiKillStagger);
	}

	double ResolveSlowMotionStartRealSeconds(const FTDSlowMotionStartInput& Input)
	{
		double StartRealSeconds = Input.RequestedStartRealSeconds;
		if (Input.LatestRagdollEntryRealSeconds.IsSet())
		{
			StartRealSeconds = FMath::Max(StartRealSeconds, Input.LatestRagdollEntryRealSeconds.GetValue() + SlowMotionRagdollLeadSeconds);
		}
		if (Input.bHasRagdollAwaitingEntry)
		{
			StartRealSeconds = FMath::Max(StartRealSeconds, Input.NowRealSeconds + SlowMotionRagdollLeadSeconds);
		}
		return FMath::Min(StartRealSeconds, Input.RequestedStartRealSeconds + FMath::Max(Input.MaxRagdollDelaySeconds, 0.));
	}

	bool ShouldDeferRagdollStart(const FTDRagdollDeferInput& Input)
	{
		if (Input.DeferredRealSeconds >= MaxRagdollDeferSeconds)
		{
			return false;
		}
		if (Input.GlobalTimeDilation < SlowTimeDilation || Input.bIsSlowMotionActive)
		{
			return true;
		}
		if (!Input.NextSlowMotionStartRealSeconds.IsSet())
		{
			return false;
		}
		const double NextStartRealSeconds = Input.NextSlowMotionStartRealSeconds.GetValue();
		const bool bLeavesRagdollLead = NextStartRealSeconds >= Input.ScheduledEntryRealSeconds + SlowMotionRagdollLeadSeconds - SlowMotionLeadToleranceSeconds;
		return !bLeavesRagdollLead && NextStartRealSeconds - Input.NowRealSeconds <= ImminentSlowMotionSeconds;
	}

	ETDDeathLandingState ResolveDeathLandingState(const FTDDeathLaunch& Launch, const FTDDeathLandingSample& Sample)
	{
		if (Sample.ElapsedSeconds >= LandingAbandonSeconds)
		{
			return ETDDeathLandingState::Abandoned;
		}
		const float RestCenterOfMassHeight = Launch.GroundHeight + Launch.RestHeight;
		const bool bIsNearRestHeight = Sample.CenterOfMassHeight <= RestCenterOfMassHeight + LandingFallbackHeightMargin;
		if (Sample.ElapsedSeconds >= LandingOverdueSeconds)
		{
			return bIsNearRestHeight ? ETDDeathLandingState::Landed : ETDDeathLandingState::Overdue;
		}
		if (bIsNearRestHeight && Sample.ElapsedSeconds >= Launch.ExpectedFallSeconds + LandingFallbackSeconds)
		{
			return ETDDeathLandingState::Landed;
		}
		if (Sample.MinVerticalSpeed > LandingMinFallSpeed)
		{
			return ETDDeathLandingState::Falling;
		}
		const float RecoverySpeed = Sample.VerticalSpeed - Sample.MinVerticalSpeed;
		if (RecoverySpeed < FMath::Max(LandingMinRecoverySpeed, LandingRecoveryRatio * FMath::Abs(Sample.MinVerticalSpeed)))
		{
			return ETDDeathLandingState::Falling;
		}
		return Sample.CenterOfMassHeight <= RestCenterOfMassHeight + LandingHeightMargin ? ETDDeathLandingState::Landed : ETDDeathLandingState::Falling;
	}

	FTDCorpseLandSound ComputeCorpseLandSound(const FTDDeathLaunch& Launch, const FTDDeathLandingSample& Sample, const float CapsuleHalfHeight)
	{
		const float UpSpeed = FMath::Max(static_cast<float>(Launch.LinearVelocity.Z), 0.f);
		const float ExpectedImpactSpeed = UpSpeed + FMath::Sqrt(2.f * EffectiveGravityAcceleration * FMath::Max(Launch.DropHeight, 0.f));
		const float PeakFallSpeed = FMath::Max(-Sample.MinVerticalSpeed, 0.f);
		const float Strength = ExpectedImpactSpeed > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(PeakFallSpeed / ExpectedImpactSpeed, MinLandVolume, 1.f) : MinLandVolume;

		FTDCorpseLandSound Sound;
		Sound.VolumeScale = Strength;
		if (CapsuleHalfHeight < SmallBodyHalfHeight)
		{
			Sound.VolumeScale = Strength * SmallBodyLandVolume;
			Sound.PitchScale = SmallBodyLandPitch;
			return Sound;
		}
		if (CapsuleHalfHeight >= LargeBodyHalfHeight)
		{
			Sound.VolumeScale = Strength * LargeBodyLandVolume;
			Sound.PitchScale = LargeBodyLandPitch;
		}
		return Sound;
	}

	float ComputeBiteAngularDamping(const bool bHasHorizontalBodyPlan)
	{
		return bHasHorizontalBodyPlan ? HorizontalBiteAngularDamping : UprightBiteAngularDamping;
	}

	bool ShouldLimitLandingRoll(const bool bHasHorizontalBodyPlan, const float CapsuleHalfHeight, const float TorsoMassRatio)
	{
		return bHasHorizontalBodyPlan && CapsuleHalfHeight <= MaxLandingRollCapsuleHalfHeight && TorsoMassRatio >= MinLandingRollTorsoMassRatio;
	}

	bool IsLandingRollTorsoBone(const FName BoneName)
	{
		const FString BoneText = BoneName.ToString();
		return BoneText.Contains(TEXT("pelvis"), ESearchCase::IgnoreCase) || BoneText.Contains(TEXT("spine"), ESearchCase::IgnoreCase);
	}

	float ComputeLandingRollSpeedExcess(const float RollRadiansPerSecond)
	{
		const float MaxRollRadiansPerSecond = FMath::DegreesToRadians(MaxLandingRollDegreesPerSecond);
		const float RollSpeed = FMath::Abs(RollRadiansPerSecond);
		if (RollSpeed <= MaxRollRadiansPerSecond)
		{
			return 0.f;
		}
		return FMath::Sign(RollRadiansPerSecond) * (RollSpeed - MaxRollRadiansPerSecond);
	}

	bool ShouldAddCorpseLandTrauma(const ETDKillRank KillRank, const float CapsuleHalfHeight, const float ViewTargetDistance)
	{
		if (ViewTargetDistance > MaxCorpseLandTraumaDistance)
		{
			return false;
		}
		const bool bIsEliteOrAbove = KillRank == ETDKillRank::Elite || KillRank == ETDKillRank::Boss;
		return bIsEliteOrAbove || CapsuleHalfHeight >= LandingTraumaHalfHeight;
	}
}
