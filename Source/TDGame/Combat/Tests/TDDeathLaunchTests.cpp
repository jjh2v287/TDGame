#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/Feedback/TDCombatFeedbackSettings.h"
#include "Combat/Feedback/TDDeathLaunchRules.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

namespace TDDeathLaunchTestsInternal
{
	constexpr float EffectiveGravity = 980.f * 1.6f;
	constexpr float AngleToleranceDegrees = 0.05f;
	constexpr float MaxDeviationDegrees = 37.f;
	constexpr float MaxPlanarSpeed = 800.f;
	constexpr float MaxTipDegreesPerSecond = 720.f;
	constexpr float MaxTiltTipDegreesPerSecond = 360.f;
	constexpr float MaxVerticalSpinDegreesPerSecond = 300.f;
	constexpr float StatusLandingDegrees = 80.f;
	constexpr float HorizontalLandingDegrees = 70.f;
	constexpr float MaxHorizontalLandingDegrees = 95.f;
	constexpr uint32 SeedCount = 200;

	struct FTDScopedDeathConsoleValue
	{
		FTDScopedDeathConsoleValue(const TCHAR* VariableName, const float Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(VariableName))
		{
			if (!Variable)
			{
				return;
			}
			PreviousValue = Variable->GetFloat();
			Variable->Set(Value, ECVF_SetByCode);
		}

		~FTDScopedDeathConsoleValue()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		float PreviousValue = 0.f;
	};

	struct FTDDeathConsoleDefaults
	{
		FTDScopedDeathConsoleValue ImmediateRagdoll{TEXT("TD.Death.ImmediateRagdoll"), 1.f};
		FTDScopedDeathConsoleValue SwingTurnScale{TEXT("TD.Death.SwingTurnScale"), 0.5f};
		FTDScopedDeathConsoleValue LaunchScale{TEXT("TD.Death.LaunchScale"), 1.f};
	};

	FTDDeathLaunchInput MakeUprightDeathInput(const float HalfHeight = 90.f, const uint32 Seed = 7)
	{
		FTDDeathLaunchInput Input;
		Input.Delivery = ETDDamageDelivery::Melee;
		Input.KillRank = ETDKillRank::Normal;
		Input.HitDirection = FVector::ForwardVector;
		Input.ActorForward = -FVector::ForwardVector;
		Input.DamageRatio = 0.5f;
		Input.UnclampedDamage = 50.f;
		Input.MaxHealth = 100.f;
		Input.CapsuleHalfHeight = HalfHeight;
		Input.ActorHeight = HalfHeight;
		Input.CenterOfMassHeight = HalfHeight;
		Input.GroundHeight = 0.f;
		Input.bHasGroundHeight = true;
		Input.VariationSeed = Seed;
		return Input;
	}

	FTDDeathLaunchInput MakeHorizontalDeathInput(const float HalfHeight = 60.f, const uint32 Seed = 7)
	{
		FTDDeathLaunchInput Input = MakeUprightDeathInput(HalfHeight, Seed);
		Input.bHasHorizontalBodyPlan = true;
		return Input;
	}

	FVector MakeDeathPlanarDirection(const float Degrees)
	{
		const float Radians = FMath::DegreesToRadians(Degrees);
		return FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.f);
	}

	float MeasureSignedLaunchDegrees(const FVector& From, const FVector& To)
	{
		const FVector FromPlanarDirection = From.GetSafeNormal2D();
		const FVector ToPlanarDirection = To.GetSafeNormal2D();
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2((FromPlanarDirection ^ ToPlanarDirection).Z, FromPlanarDirection | ToPlanarDirection)));
	}

	float MeasureLaunchAirborneDistance(const FTDDeathLaunch& Launch)
	{
		const float PlanarSpeed = static_cast<float>(Launch.LinearVelocity.Size2D());
		return PlanarSpeed * (1.f - FMath::Exp(-TDDeathLaunchRules::LaunchLinearDamping * Launch.ExpectedFallSeconds)) / TDDeathLaunchRules::LaunchLinearDamping;
	}

	float MeasureLaunchTipDegreesPerSecond(const FTDDeathLaunch& Launch)
	{
		return FMath::RadiansToDegrees(static_cast<float>(Launch.AngularVelocityRadians.Size2D()));
	}

	float MeasureLaunchLandingDegrees(const FTDDeathLaunch& Launch)
	{
		return MeasureLaunchTipDegreesPerSecond(Launch) * (1.f - FMath::Exp(-TDDeathLaunchRules::LaunchAngularDamping * Launch.ExpectedFallSeconds)) / TDDeathLaunchRules::LaunchAngularDamping;
	}

	float ComputeExpectedTipDegreesPerSecond(const FTDDeathLaunchInput& Input, const FTDDeathLaunch& Launch)
	{
		const bool bIsStatusKill = Input.Delivery == ETDDamageDelivery::Status;
		const float PlanarSpeed = static_cast<float>(Launch.LinearVelocity.Size2D());
		float TargetDegrees = 75.f + 20.f * PlanarSpeed / MaxPlanarSpeed;
		if (Input.bHasHorizontalBodyPlan)
		{
			TargetDegrees = HorizontalLandingDegrees;
		}
		if (bIsStatusKill)
		{
			TargetDegrees = StatusLandingDegrees;
		}
		float VariedDegrees = FMath::Clamp(TargetDegrees * TDDeathLaunchRules::MakeDeathLaunchVariation(Input.VariationSeed).SpinScale, 70.f, 110.f);
		if (Input.bHasHorizontalBodyPlan)
		{
			VariedDegrees = FMath::Min(VariedDegrees, MaxHorizontalLandingDegrees);
		}
		const float Coverage = 1.f - FMath::Exp(-TDDeathLaunchRules::LaunchAngularDamping * Launch.ExpectedFallSeconds);
		return FMath::Min(MaxTipDegreesPerSecond, VariedDegrees * TDDeathLaunchRules::LaunchAngularDamping / Coverage);
	}

	FVector MeasureLaunchPointVelocity(const FTDDeathLaunch& Launch, const FVector& Offset)
	{
		return Launch.LinearVelocity + (Launch.AngularVelocityRadians ^ Offset);
	}

	float ComputeExpectedLaunchFallSeconds(const float UpSpeed, const float DropHeight)
	{
		return (UpSpeed + FMath::Sqrt(UpSpeed * UpSpeed + 2.f * EffectiveGravity * DropHeight)) / EffectiveGravity;
	}

	float MeasureLaunchDistance(const FTDDeathLaunchInput& Input)
	{
		return MeasureLaunchAirborneDistance(TDDeathLaunchRules::ComputeDeathLaunch(Input));
	}

	bool IsLaunchRatioNear(const float Actual, const float Expected)
	{
		return FMath::Abs(Actual - Expected) <= 0.001f * FMath::Max(FMath::Abs(Expected), 1.f);
	}

	float ComputeKillHold(const ETDHitFeelTier Tier, const ETDKillRank KillRank, const bool bWasCritical, const float GroundAreaDistance = 0.f, const bool bIsGroundAreaKill = false, const int32 MultiKillIndex = 0, const uint32 Seed = 7)
	{
		FTDKillPoseHoldInput Input;
		Input.Tier = Tier;
		Input.KillRank = KillRank;
		Input.bWasCritical = bWasCritical;
		Input.GroundAreaDistance = GroundAreaDistance;
		Input.bIsGroundAreaKill = bIsGroundAreaKill;
		Input.MultiKillIndex = MultiKillIndex;
		Input.VariationSeed = Seed;
		return TDDeathLaunchRules::ComputeKillPoseHoldRealSeconds(Input);
	}

	double ResolveSlowMotionStart(const double RequestedStartRealSeconds, const TOptional<double>& LatestEntryRealSeconds, const double NowRealSeconds, const bool bHasRagdollAwaitingEntry)
	{
		FTDSlowMotionStartInput Input;
		Input.RequestedStartRealSeconds = RequestedStartRealSeconds;
		Input.NowRealSeconds = NowRealSeconds;
		Input.MaxRagdollDelaySeconds = 0.3;
		Input.LatestRagdollEntryRealSeconds = LatestEntryRealSeconds;
		Input.bHasRagdollAwaitingEntry = bHasRagdollAwaitingEntry;
		return TDDeathLaunchRules::ResolveSlowMotionStartRealSeconds(Input);
	}

	float MeasureLaunchVerticalSpinDegreesPerSecond(const FTDDeathLaunch& Launch)
	{
		return FMath::RadiansToDegrees(static_cast<float>(Launch.AngularVelocityRadians.Z));
	}

	float ComputeExpectedVerticalSpinDegreesPerSecond(const FTDDeathLaunchInput& Input, const FTDDeathLaunch& Launch)
	{
		const FTDDeathLaunchVariation Variation = TDDeathLaunchRules::MakeDeathLaunchVariation(Input.VariationSeed);
		const float TiltDegreesPerSecond = FMath::Min(MeasureLaunchTipDegreesPerSecond(Launch), MaxTiltTipDegreesPerSecond) * FMath::Tan(FMath::DegreesToRadians(Variation.AxisTiltDegrees));
		return FMath::Clamp(TiltDegreesPerSecond + Variation.YawDegreesPerSecond, -MaxVerticalSpinDegreesPerSecond, MaxVerticalSpinDegreesPerSecond);
	}

	uint32 FindStrongestVerticalSpinSeed()
	{
		uint32 StrongestSeed = 0;
		float StrongestSpin = 0.f;
		for (uint32 Seed = 0; Seed < 20000; ++Seed)
		{
			const FTDDeathLaunchVariation Variation = TDDeathLaunchRules::MakeDeathLaunchVariation(Seed);
			const float Spin = FMath::Abs(MaxTiltTipDegreesPerSecond * FMath::Tan(FMath::DegreesToRadians(Variation.AxisTiltDegrees)) + Variation.YawDegreesPerSecond);
			if (Spin > StrongestSpin)
			{
				StrongestSpin = Spin;
				StrongestSeed = Seed;
			}
		}
		return StrongestSeed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchSwingTurnTest, "TDGame.Combat.DeathLaunch.Direction.SwingTurn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchSwingTurnTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	struct FTDSwingTurnCase
	{
		float SwingDegrees = 0.f;
		bool bIsFinisher = false;
		float ExpectedTurnDegrees = 0.f;
	};
	const FTDSwingTurnCase Cases[] = {
		{20.f, false, 10.f},
		{40.f, false, 20.f},
		{-60.f, false, -30.f},
		{70.f, false, 35.f},
		{90.f, false, 35.f},
		{-120.f, false, -35.f},
		{160.f, false, 35.f},
		{40.f, true, 10.f},
		{-40.f, true, -10.f},
		{90.f, true, 15.f},
		{-150.f, true, -15.f}
	};
	for (const FTDSwingTurnCase& Case : Cases)
	{
		FTDDeathLaunchInput Input = MakeUprightDeathInput();
		Input.SwingDirection = MakeDeathPlanarDirection(Case.SwingDegrees);
		Input.bIsComboFinisher = Case.bIsFinisher;
		const FTDDeathLaunch Launch = TDDeathLaunchRules::ComputeDeathLaunch(Input);
		const float VariationDegrees = TDDeathLaunchRules::MakeDeathLaunchVariation(Input.VariationSeed).DirectionDegrees;
		const float ExpectedDegrees = FMath::Clamp(Case.ExpectedTurnDegrees + VariationDegrees, -MaxDeviationDegrees, MaxDeviationDegrees);
		const float MeasuredDegrees = MeasureSignedLaunchDegrees(Input.HitDirection, Launch.PlanarDirection);
		TestTrue(FString::Printf(TEXT("A %.0f degree swing turns the launch by min(%s theta, cap) plus variation (finisher %d)"), Case.SwingDegrees, Case.bIsFinisher ? TEXT("0.25") : TEXT("0.5"), Case.bIsFinisher ? 1 : 0), FMath::IsNearlyEqual(MeasuredDegrees, ExpectedDegrees, AngleToleranceDegrees));
	}

	FTDDeathLaunchInput NoSwingInput = MakeUprightDeathInput();
	const float NoSwingVariationDegrees = TDDeathLaunchRules::MakeDeathLaunchVariation(NoSwingInput.VariationSeed).DirectionDegrees;
	TestTrue(TEXT("A melee kill without a swing direction follows the hit direction with variation"), FMath::IsNearlyEqual(MeasureSignedLaunchDegrees(NoSwingInput.HitDirection, TDDeathLaunchRules::ComputeDeathLaunch(NoSwingInput).PlanarDirection), NoSwingVariationDegrees, AngleToleranceDegrees));

	FTDDeathLaunchInput DirectInput = MakeUprightDeathInput();
	DirectInput.Delivery = ETDDamageDelivery::Direct;
	DirectInput.SwingDirection = MakeDeathPlanarDirection(60.f);
	TestTrue(TEXT("A direct kill ignores the swing direction"), FMath::IsNearlyEqual(MeasureSignedLaunchDegrees(DirectInput.HitDirection, TDDeathLaunchRules::ComputeDeathLaunch(DirectInput).PlanarDirection), NoSwingVariationDegrees, AngleToleranceDegrees));

	{
		const FTDScopedDeathConsoleValue NoTurn(TEXT("TD.Death.SwingTurnScale"), 0.f);
		FTDDeathLaunchInput Input = MakeUprightDeathInput();
		Input.SwingDirection = MakeDeathPlanarDirection(90.f);
		TestTrue(TEXT("TD.Death.SwingTurnScale 0 keeps the hit direction"), FMath::IsNearlyEqual(MeasureSignedLaunchDegrees(Input.HitDirection, TDDeathLaunchRules::ComputeDeathLaunch(Input).PlanarDirection), NoSwingVariationDegrees, AngleToleranceDegrees));
	}
	{
		const FTDScopedDeathConsoleValue StrongTurn(TEXT("TD.Death.SwingTurnScale"), 0.8f);
		FTDDeathLaunchInput Input = MakeUprightDeathInput();
		Input.SwingDirection = MakeDeathPlanarDirection(30.f);
		const float ExpectedDegrees = FMath::Clamp(24.f + NoSwingVariationDegrees, -MaxDeviationDegrees, MaxDeviationDegrees);
		TestTrue(TEXT("TD.Death.SwingTurnScale 0.8 turns a 30 degree swing by 24 degrees"), FMath::IsNearlyEqual(MeasureSignedLaunchDegrees(Input.HitDirection, TDDeathLaunchRules::ComputeDeathLaunch(Input).PlanarDirection), ExpectedDegrees, AngleToleranceDegrees));
	}

	FTDDeathLaunchInput MissingHitInput = MakeUprightDeathInput();
	MissingHitInput.HitDirection = FVector::ZeroVector;
	MissingHitInput.ActorForward = FVector::RightVector;
	const FTDDeathLaunch MissingHitLaunch = TDDeathLaunchRules::ComputeDeathLaunch(MissingHitInput);
	TestTrue(TEXT("A missing hit direction launches away from the actor front"), FMath::Abs(MeasureSignedLaunchDegrees(-FVector::RightVector, MissingHitLaunch.PlanarDirection)) <= 8.f + AngleToleranceDegrees);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchSideTargetClampTest, "TDGame.Combat.DeathLaunch.Direction.SideTargetClamp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchSideTargetClampTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	const float SideSwingDegrees[] = {90.f, 150.f, -90.f, -170.f};
	bool bStaysInsideClamp = true;
	bool bMatchesClampedTurn = true;
	int32 ClampedCount = 0;
	int32 BelowTurnCapCount = 0;
	for (uint32 Seed = 1; Seed <= SeedCount; ++Seed)
	{
		for (const float SwingDegrees : SideSwingDegrees)
		{
			FTDDeathLaunchInput Input = MakeUprightDeathInput(90.f, Seed);
			Input.SwingDirection = MakeDeathPlanarDirection(SwingDegrees);
			const FTDDeathLaunch Launch = TDDeathLaunchRules::ComputeDeathLaunch(Input);
			const float MeasuredDegrees = MeasureSignedLaunchDegrees(Input.HitDirection, Launch.PlanarDirection);
			const float VariationDegrees = TDDeathLaunchRules::MakeDeathLaunchVariation(Seed).DirectionDegrees;
			const float ExpectedDegrees = FMath::Clamp(FMath::Sign(SwingDegrees) * 35.f + VariationDegrees, -MaxDeviationDegrees, MaxDeviationDegrees);
			bStaysInsideClamp = bStaysInsideClamp && FMath::Abs(MeasuredDegrees) <= MaxDeviationDegrees + AngleToleranceDegrees;
			bMatchesClampedTurn = bMatchesClampedTurn && FMath::IsNearlyEqual(MeasuredDegrees, ExpectedDegrees, AngleToleranceDegrees);
			ClampedCount += FMath::Abs(MeasuredDegrees) >= MaxDeviationDegrees - AngleToleranceDegrees ? 1 : 0;
			BelowTurnCapCount += FMath::Abs(MeasuredDegrees) < 34.f ? 1 : 0;
		}
	}
	TestTrue(TEXT("A side target never launches more than 37 degrees from the hit direction"), bStaysInsideClamp);
	TestTrue(TEXT("The side target launch is the turn cap plus variation, clamped after variation"), bMatchesClampedTurn);
	TestTrue(TEXT("Some side target launches reach the 37 degree clamp"), ClampedCount > 0);
	TestTrue(TEXT("Variation still pulls some side target launches inside the turn cap"), BelowTurnCapCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchProjectileDirectionTest, "TDGame.Combat.DeathLaunch.Direction.ProjectileFollowsSwing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchProjectileDirectionTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	bool bFollowsHomingProjectile = true;
	bool bFollowsDescendingProjectile = true;
	bool bAreaFollowsHit = true;
	bool bVerticalProjectileFollowsHit = true;
	for (uint32 Seed = 1; Seed <= SeedCount; ++Seed)
	{
		const float VariationDegrees = TDDeathLaunchRules::MakeDeathLaunchVariation(Seed).DirectionDegrees;

		FTDDeathLaunchInput HomingInput = MakeUprightDeathInput(90.f, Seed);
		HomingInput.Delivery = ETDDamageDelivery::Spell;
		HomingInput.SwingDirection = FVector::RightVector;
		const FTDDeathLaunch HomingLaunch = TDDeathLaunchRules::ComputeDeathLaunch(HomingInput);
		bFollowsHomingProjectile = bFollowsHomingProjectile && FMath::Abs(MeasureSignedLaunchDegrees(HomingInput.SwingDirection, HomingLaunch.PlanarDirection)) <= 1.f;

		FTDDeathLaunchInput DescendingInput = HomingInput;
		DescendingInput.SwingDirection = FVector(-0.6f, 0.5f, -0.62f).GetSafeNormal();
		const FTDDeathLaunch DescendingLaunch = TDDeathLaunchRules::ComputeDeathLaunch(DescendingInput);
		bFollowsDescendingProjectile = bFollowsDescendingProjectile && FMath::Abs(MeasureSignedLaunchDegrees(DescendingInput.SwingDirection, DescendingLaunch.PlanarDirection)) <= 1.f;

		FTDDeathLaunchInput AreaInput = MakeUprightDeathInput(90.f, Seed);
		AreaInput.Delivery = ETDDamageDelivery::Spell;
		const float AreaDegrees = MeasureSignedLaunchDegrees(AreaInput.HitDirection, TDDeathLaunchRules::ComputeDeathLaunch(AreaInput).PlanarDirection);
		bAreaFollowsHit = bAreaFollowsHit && FMath::IsNearlyEqual(AreaDegrees, VariationDegrees, AngleToleranceDegrees) && FMath::Abs(AreaDegrees) <= 8.f + AngleToleranceDegrees;

		FTDDeathLaunchInput VerticalInput = AreaInput;
		VerticalInput.SwingDirection = FVector(0.1f, 0.15f, -1.f).GetSafeNormal();
		const float VerticalDegrees = MeasureSignedLaunchDegrees(VerticalInput.HitDirection, TDDeathLaunchRules::ComputeDeathLaunch(VerticalInput).PlanarDirection);
		bVerticalProjectileFollowsHit = bVerticalProjectileFollowsHit && FMath::IsNearlyEqual(VerticalDegrees, VariationDegrees, AngleToleranceDegrees);
	}
	TestTrue(TEXT("A homing projectile bent 90 degrees launches within 1 degree of its travel"), bFollowsHomingProjectile);
	TestTrue(TEXT("A descending projectile launches along its planar travel"), bFollowsDescendingProjectile);
	TestTrue(TEXT("An area spell without travel launches along the hit direction with variation"), bAreaFollowsHit);
	TestTrue(TEXT("A nearly vertical projectile falls back to the hit direction"), bVerticalProjectileFollowsHit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchSizeScalesTest, "TDGame.Combat.DeathLaunch.Size.Scales", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchSizeScalesTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	const float OverkillRatios[] = {0.f, 0.25f, 0.5f, 0.75f, 1.f};
	float PreviousDistance = -1.f;
	bool bIsMonotonic = true;
	for (const float OverkillRatio : OverkillRatios)
	{
		FTDDeathLaunchInput Input = MakeUprightDeathInput();
		Input.DamageRatio = OverkillRatio;
		Input.UnclampedDamage = 0.f;
		const float Distance = MeasureLaunchDistance(Input);
		bIsMonotonic = bIsMonotonic && Distance > PreviousDistance;
		PreviousDistance = Distance;
	}
	TestTrue(TEXT("Launch distance grows with the overkill ratio"), bIsMonotonic);

	FTDDeathLaunchInput FullInput = MakeUprightDeathInput();
	FullInput.DamageRatio = 1.f;
	FullInput.UnclampedDamage = 0.f;
	FTDDeathLaunchInput BeyondFullInput = FullInput;
	BeyondFullInput.DamageRatio = 1.5f;
	TestTrue(TEXT("The overkill ratio stops at 1"), IsLaunchRatioNear(MeasureLaunchDistance(BeyondFullInput), MeasureLaunchDistance(FullInput)));

	FTDDeathLaunchInput RatioInput = MakeUprightDeathInput();
	RatioInput.DamageRatio = 0.8f;
	RatioInput.UnclampedDamage = 0.f;
	FTDDeathLaunchInput UnclampedInput = MakeUprightDeathInput();
	UnclampedInput.DamageRatio = 0.1f;
	UnclampedInput.UnclampedDamage = 80.f;
	TestTrue(TEXT("Unclamped damage over max health counts as the overkill ratio"), IsLaunchRatioNear(MeasureLaunchDistance(UnclampedInput), MeasureLaunchDistance(RatioInput)));

	FTDDeathLaunchInput LightInput = MakeUprightDeathInput();
	LightInput.DamageRatio = 0.f;
	LightInput.UnclampedDamage = 0.f;
	const float LightDistance = MeasureLaunchDistance(LightInput);
	FTDDeathLaunchInput CriticalInput = LightInput;
	CriticalInput.bWasCritical = true;
	TestTrue(TEXT("A critical kill flies 1.1 times as far"), IsLaunchRatioNear(MeasureLaunchDistance(CriticalInput), LightDistance * 1.1f));
	FTDDeathLaunchInput FinisherInput = LightInput;
	FinisherInput.bIsComboFinisher = true;
	TestTrue(TEXT("A melee combo finisher flies 1.8 times as far"), IsLaunchRatioNear(MeasureLaunchDistance(FinisherInput), LightDistance * 1.8f));
	FTDDeathLaunchInput CriticalFinisherInput = FinisherInput;
	CriticalFinisherInput.bWasCritical = true;
	TestTrue(TEXT("Critical and finisher scales multiply once each"), IsLaunchRatioNear(MeasureLaunchDistance(CriticalFinisherInput), LightDistance * 1.98f));

	FTDDeathLaunchInput SpellInput = LightInput;
	SpellInput.Delivery = ETDDamageDelivery::Spell;
	FTDDeathLaunchInput SpellFinisherInput = SpellInput;
	SpellFinisherInput.bIsComboFinisher = true;
	TestTrue(TEXT("Only melee delivery counts as a combo finisher"), IsLaunchRatioNear(MeasureLaunchDistance(SpellFinisherInput), MeasureLaunchDistance(SpellInput)));

	const float ReferenceDistance = MeasureLaunchDistance(MakeUprightDeathInput(90.f));
	TestTrue(TEXT("A smaller body does not fly farther than the reference size"), IsLaunchRatioNear(MeasureLaunchDistance(MakeUprightDeathInput(45.f)), ReferenceDistance));
	TestTrue(TEXT("A body of half height 120 flies 0.75 times as far"), IsLaunchRatioNear(MeasureLaunchDistance(MakeUprightDeathInput(120.f)), ReferenceDistance * 0.75f));
	TestTrue(TEXT("A huge body flies at least 0.55 times as far"), IsLaunchRatioNear(MeasureLaunchDistance(MakeUprightDeathInput(300.f)), ReferenceDistance * 0.55f));

	FTDDeathLaunchInput EliteInput = MakeUprightDeathInput();
	EliteInput.KillRank = ETDKillRank::Elite;
	TestTrue(TEXT("An elite flies as far as a normal monster"), IsLaunchRatioNear(MeasureLaunchDistance(EliteInput), ReferenceDistance));
	FTDDeathLaunchInput UnrankedInput = MakeUprightDeathInput();
	UnrankedInput.KillRank = ETDKillRank::None;
	TestTrue(TEXT("An unranked victim flies as far as a normal monster"), IsLaunchRatioNear(MeasureLaunchDistance(UnrankedInput), ReferenceDistance));
	FTDDeathLaunchInput BossInput = MakeUprightDeathInput();
	BossInput.KillRank = ETDKillRank::Boss;
	TestTrue(TEXT("A boss flies half as far"), IsLaunchRatioNear(MeasureLaunchDistance(BossInput), ReferenceDistance * 0.5f));
	FTDDeathLaunchInput PlayerInput = MakeUprightDeathInput();
	PlayerInput.bIsPlayer = true;
	TestTrue(TEXT("A player flies half as far"), IsLaunchRatioNear(MeasureLaunchDistance(PlayerInput), ReferenceDistance * 0.5f));

	bool bNeverExceedsCap = true;
	for (uint32 Seed = 1; Seed <= SeedCount; ++Seed)
	{
		FTDDeathLaunchInput StrongInput = MakeUprightDeathInput(40.f, Seed);
		StrongInput.DamageRatio = 1.f;
		StrongInput.bWasCritical = true;
		StrongInput.bIsComboFinisher = true;
		bNeverExceedsCap = bNeverExceedsCap && TDDeathLaunchRules::ComputeDeathLaunch(StrongInput).LinearVelocity.Size2D() <= MaxPlanarSpeed + 0.01f;
	}
	TestTrue(TEXT("The planar launch speed never exceeds 800"), bNeverExceedsCap);
	{
		const FTDScopedDeathConsoleValue HugeLaunch(TEXT("TD.Death.LaunchScale"), 5.f);
		FTDDeathLaunchInput HugeInput = MakeUprightDeathInput();
		HugeInput.DamageRatio = 1.f;
		TestTrue(TEXT("A huge launch stops at 800"), FMath::IsNearlyEqual(static_cast<float>(TDDeathLaunchRules::ComputeDeathLaunch(HugeInput).LinearVelocity.Size2D()), MaxPlanarSpeed, 0.01f));
	}
	{
		const FTDScopedDeathConsoleValue HalfLaunch(TEXT("TD.Death.LaunchScale"), 0.5f);
		TestTrue(TEXT("TD.Death.LaunchScale scales the airborne distance"), IsLaunchRatioNear(MeasureLaunchDistance(MakeUprightDeathInput()), ReferenceDistance * 0.5f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchFlightTest, "TDGame.Combat.DeathLaunch.Flight.ClosedForm", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchFlightTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	struct FTDFlightCase
	{
		const TCHAR* Name = TEXT("");
		FTDDeathLaunchInput Input;
		float ExpectedUpSpeed = 0.f;
		float ExpectedRestHeight = 0.f;
	};

	FTDFlightCase UprightCase{TEXT("upright"), MakeUprightDeathInput(90.f), 100.f, 0.18f * 90.f};
	FTDFlightCase HorizontalCase{TEXT("horizontal"), MakeHorizontalDeathInput(60.f), 100.f, 0.45f * 60.f};
	FTDFlightCase ChopCase{TEXT("downward chop"), MakeUprightDeathInput(90.f), 30.f, 0.18f * 90.f};
	ChopCase.Input.SwingDirection = FVector(0.5f, 0.f, -0.8f).GetSafeNormal();
	FTDFlightCase ShallowSwingCase{TEXT("shallow downward swing"), MakeUprightDeathInput(90.f), 100.f, 0.18f * 90.f};
	ShallowSwingCase.Input.SwingDirection = FVector(0.95f, 0.f, -0.2f).GetSafeNormal();
	FTDFlightCase StatusCase{TEXT("status"), MakeUprightDeathInput(90.f), 0.f, 0.18f * 90.f};
	StatusCase.Input.Delivery = ETDDamageDelivery::Status;
	FTDFlightCase TallCase{TEXT("tall upright"), MakeUprightDeathInput(150.f), 100.f, 0.18f * 150.f};
	FTDFlightCase UprightRatioCase{TEXT("upright species rest ratio"), MakeUprightDeathInput(90.f), 100.f, 0.3f * 90.f};
	UprightRatioCase.Input.RestHeightRatio = 0.3f;
	FTDFlightCase HorizontalRatioCase{TEXT("horizontal species rest ratio"), MakeHorizontalDeathInput(122.f), 100.f, 0.15f * 122.f};
	HorizontalRatioCase.Input.RestHeightRatio = 0.15f;

	for (const FTDFlightCase& Case : {UprightCase, HorizontalCase, ChopCase, ShallowSwingCase, StatusCase, TallCase, UprightRatioCase, HorizontalRatioCase})
	{
		const FTDDeathLaunch Launch = TDDeathLaunchRules::ComputeDeathLaunch(Case.Input);
		const float ExpectedDropHeight = FMath::Max(10.f, Case.Input.CenterOfMassHeight - Case.Input.GroundHeight - Case.ExpectedRestHeight);
		const float ExpectedFallSeconds = ComputeExpectedLaunchFallSeconds(Case.ExpectedUpSpeed, ExpectedDropHeight);
		TestTrue(FString::Printf(TEXT("The %s rest height follows the body plan"), Case.Name), FMath::IsNearlyEqual(Launch.RestHeight, Case.ExpectedRestHeight, 0.01f));
		TestTrue(FString::Printf(TEXT("The %s drop height is COM minus ground minus rest height"), Case.Name), FMath::IsNearlyEqual(Launch.DropHeight, ExpectedDropHeight, 0.01f));
		TestTrue(FString::Printf(TEXT("The %s up speed is %.0f"), Case.Name, Case.ExpectedUpSpeed), FMath::IsNearlyEqual(static_cast<float>(Launch.LinearVelocity.Z), Case.ExpectedUpSpeed, 0.01f));
		TestTrue(FString::Printf(TEXT("The %s fall time matches the closed form"), Case.Name), FMath::IsNearlyEqual(Launch.ExpectedFallSeconds, ExpectedFallSeconds, 0.0001f));
		TestTrue(FString::Printf(TEXT("The %s launch adds 0.6 g of extra gravity"), Case.Name), Launch.ExtraGravity.Equals(FVector(0.f, 0.f, -588.f), 0.01f));
		TestTrue(FString::Printf(TEXT("The %s launch watches for landing"), Case.Name), Launch.bWatchesLanding);
	}

	const FTDDeathLaunch UprightLaunch = TDDeathLaunchRules::ComputeDeathLaunch(UprightCase.Input);
	TestTrue(TEXT("An upright body of half height 90 flies about 0.38 seconds"), UprightLaunch.ExpectedFallSeconds > 0.36f && UprightLaunch.ExpectedFallSeconds < 0.40f);

	const FTDDeathLaunchInput HorizontalDefaultInput = MakeHorizontalDeathInput(122.f);
	const FTDDeathLaunch HorizontalDefaultLaunch = TDDeathLaunchRules::ComputeDeathLaunch(HorizontalDefaultInput);
	const FTDDeathLaunch HorizontalRatioLaunch = TDDeathLaunchRules::ComputeDeathLaunch(HorizontalRatioCase.Input);
	TestTrue(TEXT("A rest height ratio of 0 uses the horizontal default of 0.45"), FMath::IsNearlyEqual(HorizontalDefaultLaunch.RestHeight, 0.45f * 122.f, 0.01f));
	TestTrue(TEXT("A lower species rest height ratio gives a longer fall"), HorizontalRatioLaunch.ExpectedFallSeconds > HorizontalDefaultLaunch.ExpectedFallSeconds + 0.05f);
	FTDDeathLaunchInput UprightRatioOnHorizontalInput = HorizontalDefaultInput;
	UprightRatioOnHorizontalInput.RestHeightRatio = 0.18f;
	FTDDeathLaunchInput UprightDefaultInput = HorizontalDefaultInput;
	UprightDefaultInput.bHasHorizontalBodyPlan = false;
	TestTrue(TEXT("A species rest height ratio overrides the body plan default"), FMath::IsNearlyEqual(TDDeathLaunchRules::ComputeDeathLaunch(UprightRatioOnHorizontalInput).RestHeight, TDDeathLaunchRules::ComputeDeathLaunch(UprightDefaultInput).RestHeight, 0.01f));

	FTDDeathLaunchInput NoGroundInput = MakeUprightDeathInput(90.f);
	NoGroundInput.bHasGroundHeight = false;
	NoGroundInput.GroundHeight = 5000.f;
	NoGroundInput.ActorHeight = 200.f;
	NoGroundInput.CenterOfMassHeight = 200.f;
	const FTDDeathLaunch NoGroundLaunch = TDDeathLaunchRules::ComputeDeathLaunch(NoGroundInput);
	TestTrue(TEXT("A missed ground trace uses the actor height minus the half height"), FMath::IsNearlyEqual(NoGroundLaunch.GroundHeight, 110.f, 0.01f));
	TestTrue(TEXT("The fallback ground gives the standing drop height"), FMath::IsNearlyEqual(NoGroundLaunch.DropHeight, 90.f - 0.18f * 90.f, 0.01f));

	FTDDeathLaunchInput LowInput = MakeUprightDeathInput(90.f);
	LowInput.CenterOfMassHeight = 5.f;
	TestTrue(TEXT("The drop height never goes below 10"), FMath::IsNearlyEqual(TDDeathLaunchRules::ComputeDeathLaunch(LowInput).DropHeight, 10.f, 0.01f));

	bool bMatchesAirborneDistance = true;
	for (uint32 Seed = 1; Seed <= SeedCount; ++Seed)
	{
		const float DistanceScale = TDDeathLaunchRules::MakeDeathLaunchVariation(Seed).DistanceScale;
		const FTDDeathLaunchInput Inputs[] = {MakeUprightDeathInput(90.f, Seed), MakeHorizontalDeathInput(60.f, Seed), MakeUprightDeathInput(130.f, Seed)};
		for (const FTDDeathLaunchInput& Input : Inputs)
		{
			const FTDDeathLaunch Launch = TDDeathLaunchRules::ComputeDeathLaunch(Input);
			if (Launch.LinearVelocity.Size2D() >= MaxPlanarSpeed - 0.01f)
			{
				continue;
			}
			const float SizeScale = FMath::Clamp(90.f / Input.CapsuleHalfHeight, 0.55f, 1.f);
			const float ExpectedDistance = (110.f + 60.f * 0.5f) * SizeScale * DistanceScale;
			bMatchesAirborneDistance = bMatchesAirborneDistance && FMath::Abs(MeasureLaunchAirborneDistance(Launch) - ExpectedDistance) <= 0.01f * ExpectedDistance;
		}
	}
	TestTrue(TEXT("The planar speed covers the planned airborne distance within 1 percent"), bMatchesAirborneDistance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchUprightRotationTest, "TDGame.Combat.DeathLaunch.Rotation.UprightTipsHeadAlongLaunch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchUprightRotationTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	const float HitDegrees[] = {0.f, 90.f, 225.f};
	bool bHeadLeadsFeet = true;
	bool bHeadLeadsBody = true;
	for (uint32 Seed = 1; Seed <= SeedCount; ++Seed)
	{
		for (const float Degrees : HitDegrees)
		{
			FTDDeathLaunchInput Input = MakeUprightDeathInput(90.f, Seed);
			Input.HitDirection = MakeDeathPlanarDirection(Degrees);
			Input.ActorForward = -Input.HitDirection;
			Input.SwingDirection = MakeDeathPlanarDirection(Degrees + 70.f);
			const FTDDeathLaunch Launch = TDDeathLaunchRules::ComputeDeathLaunch(Input);
			const FVector HeadOffset = FVector::UpVector * (0.8f * Input.CapsuleHalfHeight);
			const double HeadSpeed = MeasureLaunchPointVelocity(Launch, HeadOffset) | Launch.PlanarDirection;
			const double FootSpeed = MeasureLaunchPointVelocity(Launch, -HeadOffset) | Launch.PlanarDirection;
			bHeadLeadsFeet = bHeadLeadsFeet && HeadSpeed > FootSpeed;
			bHeadLeadsBody = bHeadLeadsBody && HeadSpeed > (Launch.LinearVelocity | Launch.PlanarDirection);
		}
	}
	TestTrue(TEXT("An upright body's head tip moves along the launch faster than its foot tip"), bHeadLeadsFeet);
	TestTrue(TEXT("An upright body's head tips over toward the launch direction"), bHeadLeadsBody);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchHorizontalRotationTest, "TDGame.Combat.DeathLaunch.Rotation.HorizontalRollsWithoutNoseLift", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchHorizontalRotationTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	bool bFrontHitKeepsNoseDown = true;
	bool bBackHitKeepsNoseDown = true;
	bool bHasNoPitch = true;
	bool bSideHitRollsTowardLaunch = true;
	bool bUsesBothFrontRollSides = false;
	float FirstFrontRollSign = 0.f;
	for (uint32 Seed = 1; Seed <= SeedCount; ++Seed)
	{
		FTDDeathLaunchInput FrontInput = MakeHorizontalDeathInput(60.f, Seed);
		FrontInput.Delivery = ETDDamageDelivery::Direct;
		FrontInput.HitDirection = FVector::ForwardVector;
		FrontInput.ActorForward = -FVector::ForwardVector;
		const FTDDeathLaunch FrontLaunch = TDDeathLaunchRules::ComputeDeathLaunch(FrontInput);
		const FVector FrontHead = FrontInput.ActorForward * FrontInput.CapsuleHalfHeight;
		bFrontHitKeepsNoseDown = bFrontHitKeepsNoseDown && MeasureLaunchPointVelocity(FrontLaunch, FrontHead).Z <= FrontLaunch.LinearVelocity.Z + 0.01;
		const FVector FrontLateral = FrontInput.ActorForward ^ FVector::UpVector;
		bHasNoPitch = bHasNoPitch && FMath::Abs(FrontLaunch.AngularVelocityRadians | FrontLateral) <= 0.0001;
		const float FrontRollSign = static_cast<float>(FMath::Sign(FrontLaunch.AngularVelocityRadians | FrontInput.ActorForward));
		if (Seed == 1)
		{
			FirstFrontRollSign = FrontRollSign;
		}
		bUsesBothFrontRollSides = bUsesBothFrontRollSides || FrontRollSign != FirstFrontRollSign;

		FTDDeathLaunchInput BackInput = FrontInput;
		BackInput.ActorForward = FVector::ForwardVector;
		BackInput.Delivery = ETDDamageDelivery::Melee;
		BackInput.SwingDirection = FVector::RightVector;
		const FTDDeathLaunch BackLaunch = TDDeathLaunchRules::ComputeDeathLaunch(BackInput);
		const FVector BackHead = BackInput.ActorForward * BackInput.CapsuleHalfHeight;
		bBackHitKeepsNoseDown = bBackHitKeepsNoseDown && MeasureLaunchPointVelocity(BackLaunch, BackHead).Z <= BackLaunch.LinearVelocity.Z + 0.01;
		bHasNoPitch = bHasNoPitch && FMath::Abs(BackLaunch.AngularVelocityRadians | (BackInput.ActorForward ^ FVector::UpVector)) <= 0.0001;

		FTDDeathLaunchInput SideInput = MakeHorizontalDeathInput(60.f, Seed);
		SideInput.Delivery = ETDDamageDelivery::Direct;
		SideInput.HitDirection = FVector::ForwardVector;
		SideInput.ActorForward = FVector::RightVector;
		const FTDDeathLaunch SideLaunch = TDDeathLaunchRules::ComputeDeathLaunch(SideInput);
		const FVector SideTopVelocity = SideLaunch.AngularVelocityRadians ^ FVector::UpVector;
		bSideHitRollsTowardLaunch = bSideHitRollsTowardLaunch && (SideTopVelocity | SideLaunch.PlanarDirection) > 0.;
		bHasNoPitch = bHasNoPitch && FMath::Abs(SideLaunch.AngularVelocityRadians | (SideInput.ActorForward ^ FVector::UpVector)) <= 0.0001;
	}
	TestTrue(TEXT("A front hit never lifts a horizontal body's head faster than the launch up speed"), bFrontHitKeepsNoseDown);
	TestTrue(TEXT("A back hit with a side swing never lifts a horizontal body's head"), bBackHitKeepsNoseDown);
	TestTrue(TEXT("A horizontal body never pitches"), bHasNoPitch);
	TestTrue(TEXT("A side hit rolls a horizontal body's back toward the launch"), bSideHitRollsTowardLaunch);
	TestTrue(TEXT("Front hits pick the roll side from the seed"), bUsesBothFrontRollSides);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchStatusRotationTest, "TDGame.Combat.DeathLaunch.Rotation.StatusFallsForward", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchStatusRotationTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	bool bHasNoLinearLaunch = true;
	bool bFallsTowardFront = true;
	bool bHeadMovesForward = true;
	bool bHorizontalKeepsNoseDown = true;
	for (uint32 Seed = 1; Seed <= SeedCount; ++Seed)
	{
		FTDDeathLaunchInput Input = MakeUprightDeathInput(90.f, Seed);
		Input.Delivery = ETDDamageDelivery::Status;
		Input.ActorForward = FVector::RightVector;
		const FTDDeathLaunch Launch = TDDeathLaunchRules::ComputeDeathLaunch(Input);
		bHasNoLinearLaunch = bHasNoLinearLaunch && Launch.LinearVelocity.IsNearlyZero(0.001);
		bFallsTowardFront = bFallsTowardFront && Launch.PlanarDirection.Equals(Input.ActorForward, 0.001);
		const FVector HeadOffset = FVector::UpVector * (0.8f * Input.CapsuleHalfHeight);
		bHeadMovesForward = bHeadMovesForward && (MeasureLaunchPointVelocity(Launch, HeadOffset) | Input.ActorForward) > 0.;

		FTDDeathLaunchInput HorizontalInput = MakeHorizontalDeathInput(60.f, Seed);
		HorizontalInput.Delivery = ETDDamageDelivery::Status;
		const FTDDeathLaunch HorizontalLaunch = TDDeathLaunchRules::ComputeDeathLaunch(HorizontalInput);
		const FVector Head = HorizontalInput.ActorForward * HorizontalInput.CapsuleHalfHeight;
		bHorizontalKeepsNoseDown = bHorizontalKeepsNoseDown && HorizontalLaunch.LinearVelocity.IsNearlyZero(0.001) && MeasureLaunchPointVelocity(HorizontalLaunch, Head).Z <= 0.01;
	}
	TestTrue(TEXT("A status kill has no linear launch"), bHasNoLinearLaunch);
	TestTrue(TEXT("A status kill falls toward the actor front"), bFallsTowardFront);
	TestTrue(TEXT("A status kill tips an upright head toward the actor front"), bHeadMovesForward);
	TestTrue(TEXT("A status kill collapses a horizontal body without lifting its head"), bHorizontalKeepsNoseDown);

	FTDDeathLaunchInput StatusInput = MakeUprightDeathInput(90.f);
	StatusInput.Delivery = ETDDamageDelivery::Status;
	const FTDDeathLaunch StatusLaunch = TDDeathLaunchRules::ComputeDeathLaunch(StatusInput);
	const float ExpectedFallSeconds = FMath::Sqrt(2.f * StatusLaunch.DropHeight / EffectiveGravity);
	TestTrue(TEXT("A status kill falls from rest"), FMath::IsNearlyEqual(StatusLaunch.ExpectedFallSeconds, ExpectedFallSeconds, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchLandingAngleTest, "TDGame.Combat.DeathLaunch.Rotation.LandingAngle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchLandingAngleTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	const float HalfHeights[] = {40.f, 60.f, 90.f, 130.f, 200.f};
	const float OverkillRatios[] = {0.f, 0.5f, 1.f};
	const ETDDamageDelivery Deliveries[] = {ETDDamageDelivery::Melee, ETDDamageDelivery::Spell, ETDDamageDelivery::Status};
	bool bLandsInRange = true;
	bool bRespectsSpinCap = true;
	float MinLandingDegrees = TNumericLimits<float>::Max();
	float MaxLandingDegrees = 0.f;
	for (uint32 Seed = 1; Seed <= 20; ++Seed)
	{
		for (const float HalfHeight : HalfHeights)
		{
			for (const float OverkillRatio : OverkillRatios)
			{
				for (const ETDDamageDelivery Delivery : Deliveries)
				{
					for (int32 BodyPlan = 0; BodyPlan < 2; ++BodyPlan)
					{
						for (int32 Strength = 0; Strength < 3; ++Strength)
						{
							FTDDeathLaunchInput Input = BodyPlan == 0 ? MakeUprightDeathInput(HalfHeight, Seed) : MakeHorizontalDeathInput(HalfHeight, Seed);
							Input.Delivery = Delivery;
							Input.DamageRatio = OverkillRatio;
							Input.UnclampedDamage = 0.f;
							Input.SwingDirection = MakeDeathPlanarDirection(50.f);
							Input.bWasCritical = Strength > 0;
							Input.bIsComboFinisher = Strength > 1;
							const FTDDeathLaunch Launch = TDDeathLaunchRules::ComputeDeathLaunch(Input);
							const float LandingDegrees = MeasureLaunchLandingDegrees(Launch);
							MinLandingDegrees = FMath::Min(MinLandingDegrees, LandingDegrees);
							MaxLandingDegrees = FMath::Max(MaxLandingDegrees, LandingDegrees);
							bLandsInRange = bLandsInRange && LandingDegrees >= 70.f - 0.01f && LandingDegrees <= 110.f + 0.01f;
							bRespectsSpinCap = bRespectsSpinCap && MeasureLaunchTipDegreesPerSecond(Launch) <= MaxTipDegreesPerSecond + 0.01f;
						}
					}
				}
			}
		}
	}
	TestTrue(FString::Printf(TEXT("The predicted landing angle stays between 70 and 110 degrees (seen %.1f to %.1f)"), MinLandingDegrees, MaxLandingDegrees), bLandsInRange);
	TestTrue(TEXT("The tipping spin never exceeds 720 degrees per second"), bRespectsSpinCap);
	TestTrue(TEXT("Stronger launches land at steeper angles"), MaxLandingDegrees > MinLandingDegrees + 20.f);

	const float DropOffsets[] = {10.f, 12.5f, 15.f};
	const FVector PlanarSwing = MakeDeathPlanarDirection(50.f);
	const FVector ChopSwing = FVector(0.5f, 0.f, -0.8f).GetSafeNormal();
	bool bShortLandsInRange = true;
	bool bShortSpinMatchesRule = true;
	int32 ShortCappedCount = 0;
	int32 ShortBelowFloorCount = 0;
	float MinShortLandingDegrees = TNumericLimits<float>::Max();
	float MaxShortLandingDegrees = 0.f;
	for (uint32 Seed = 1; Seed <= 20; ++Seed)
	{
		for (const float HalfHeight : HalfHeights)
		{
			for (int32 BodyPlan = 0; BodyPlan < 2; ++BodyPlan)
			{
				for (const float DropOffset : DropOffsets)
				{
					for (int32 Motion = 0; Motion < 3; ++Motion)
					{
						for (int32 Strength = 0; Strength < 3; ++Strength)
						{
							FTDDeathLaunchInput Input = BodyPlan == 0 ? MakeUprightDeathInput(HalfHeight, Seed) : MakeHorizontalDeathInput(HalfHeight, Seed);
							const float BodyRestHeight = HalfHeight * (BodyPlan == 0 ? 0.18f : 0.45f);
							Input.GroundHeight = 40.f;
							Input.CenterOfMassHeight = Input.GroundHeight + BodyRestHeight + DropOffset;
							Input.Delivery = Motion == 2 ? ETDDamageDelivery::Status : ETDDamageDelivery::Melee;
							Input.SwingDirection = Motion == 1 ? ChopSwing : PlanarSwing;
							Input.DamageRatio = Strength > 0 ? 1.f : 0.f;
							Input.UnclampedDamage = 0.f;
							Input.bWasCritical = Strength > 0;
							Input.bIsComboFinisher = Strength > 1;
							const FTDDeathLaunch Launch = TDDeathLaunchRules::ComputeDeathLaunch(Input);
							const float TipDegreesPerSecond = MeasureLaunchTipDegreesPerSecond(Launch);
							const float LandingDegrees = MeasureLaunchLandingDegrees(Launch);
							const bool bIsSpinCapped = FMath::IsNearlyEqual(TipDegreesPerSecond, MaxTipDegreesPerSecond, 0.05f);
							MinShortLandingDegrees = FMath::Min(MinShortLandingDegrees, LandingDegrees);
							MaxShortLandingDegrees = FMath::Max(MaxShortLandingDegrees, LandingDegrees);
							ShortCappedCount += bIsSpinCapped ? 1 : 0;
							ShortBelowFloorCount += LandingDegrees < 70.f - 0.01f ? 1 : 0;
							bShortSpinMatchesRule = bShortSpinMatchesRule && FMath::IsNearlyEqual(TipDegreesPerSecond, ComputeExpectedTipDegreesPerSecond(Input, Launch), 0.05f);
							bShortLandsInRange = bShortLandsInRange && LandingDegrees <= 110.f + 0.01f && (LandingDegrees >= 70.f - 0.01f || bIsSpinCapped);
						}
					}
				}
			}
		}
	}
	TestTrue(TEXT("A short drop spins at min(720, varied target angle over the fall coverage)"), bShortSpinMatchesRule);
	TestTrue(FString::Printf(TEXT("A short drop lands between 70 and 110 degrees, or below 70 only at the 720 cap (seen %.1f to %.1f, %d below 70)"), MinShortLandingDegrees, MaxShortLandingDegrees, ShortBelowFloorCount), bShortLandsInRange);
	TestTrue(TEXT("Some short drops need the full 720 degree spin"), ShortCappedCount > 0);

	const float CapFloorFallSeconds = -FMath::Loge(1.f - 70.f * TDDeathLaunchRules::LaunchAngularDamping / MaxTipDegreesPerSecond) / TDDeathLaunchRules::LaunchAngularDamping;
	const float ShortestFallSeconds = ComputeExpectedLaunchFallSeconds(0.f, 10.f);
	TestTrue(FString::Printf(TEXT("The 720 cap reaches 70 degrees only for falls of %.3f s or more, and the shortest rule fall is %.3f s"), CapFloorFallSeconds, ShortestFallSeconds), ShortestFallSeconds > CapFloorFallSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchPresentationTableTest, "TDGame.Combat.DeathLaunch.Presentation.Table", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchPresentationTableTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	TestTrue(TEXT("TD.Death.ImmediateRagdoll is registered"), IConsoleManager::Get().FindConsoleVariable(TEXT("TD.Death.ImmediateRagdoll")) != nullptr);
	TestTrue(TEXT("TD.Death.SwingTurnScale is registered"), IConsoleManager::Get().FindConsoleVariable(TEXT("TD.Death.SwingTurnScale")) != nullptr);
	TestTrue(TEXT("TD.Death.LaunchScale is registered"), IConsoleManager::Get().FindConsoleVariable(TEXT("TD.Death.LaunchScale")) != nullptr);

	struct FTDPresentationCase
	{
		const TCHAR* Name = TEXT("");
		bool bIsImmediateRagdollEnabled = true;
		bool bIsBodyAirborne = false;
		bool bHasRagdollPhysicsAsset = true;
		bool bShouldPlayDeathClipBeforeRagdoll = false;
		bool bHasDeathClip = true;
		bool bShouldRagdollAfterDeathClip = true;
		bool bShouldUseRagdollWithoutDeathClip = true;
		ETDDeathPresentation Expected = ETDDeathPresentation::Ragdoll;
	};
	const FTDPresentationCase Cases[] = {
		{TEXT("An airborne body without a physics asset"), true, true, false, false, true, false, false, ETDDeathPresentation::Ragdoll},
		{TEXT("An airborne body of a clip-first species"), true, true, true, true, true, true, false, ETDDeathPresentation::Ragdoll},
		{TEXT("An airborne body with immediate ragdoll off"), false, true, true, false, true, false, false, ETDDeathPresentation::Ragdoll},
		{TEXT("A grounded body without a physics asset and with a clip"), true, false, false, false, true, true, true, ETDDeathPresentation::ClipOnly},
		{TEXT("A grounded body without a physics asset or clip"), true, false, false, false, false, false, true, ETDDeathPresentation::ClipOnly},
		{TEXT("A default body with a clip"), true, false, true, false, true, false, false, ETDDeathPresentation::Ragdoll},
		{TEXT("A default body without a clip"), true, false, true, false, false, false, false, ETDDeathPresentation::Ragdoll},
		{TEXT("A clip-first species with a clip that ragdolls after it"), true, false, true, true, true, true, false, ETDDeathPresentation::ClipThenRagdoll},
		{TEXT("A clip-first species with a clip that stays in the clip"), true, false, true, true, true, false, true, ETDDeathPresentation::ClipOnly},
		{TEXT("A clip-first species without a clip that ragdolls"), true, false, true, true, false, true, true, ETDDeathPresentation::Ragdoll},
		{TEXT("A clip-first species without a clip that never ragdolls"), true, false, true, true, false, true, false, ETDDeathPresentation::ClipOnly},
		{TEXT("Immediate ragdoll off with a clip that ragdolls after it"), false, false, true, false, true, true, false, ETDDeathPresentation::ClipThenRagdoll},
		{TEXT("Immediate ragdoll off with a clip that stays in the clip"), false, false, true, false, true, false, true, ETDDeathPresentation::ClipOnly},
		{TEXT("Immediate ragdoll off without a clip that ragdolls"), false, false, true, false, false, false, true, ETDDeathPresentation::Ragdoll},
		{TEXT("Immediate ragdoll off without a clip that never ragdolls"), false, false, true, false, false, true, false, ETDDeathPresentation::ClipOnly},
		{TEXT("Immediate ragdoll off without a physics asset"), false, false, false, false, false, false, true, ETDDeathPresentation::ClipOnly}
	};
	for (const FTDPresentationCase& Case : Cases)
	{
		const FTDScopedDeathConsoleValue ImmediateRagdoll(TEXT("TD.Death.ImmediateRagdoll"), Case.bIsImmediateRagdollEnabled ? 1.f : 0.f);
		FTDDeathPresentationInput Input;
		Input.bIsBodyAirborne = Case.bIsBodyAirborne;
		Input.bHasRagdollPhysicsAsset = Case.bHasRagdollPhysicsAsset;
		Input.bShouldPlayDeathClipBeforeRagdoll = Case.bShouldPlayDeathClipBeforeRagdoll;
		Input.bHasDeathClip = Case.bHasDeathClip;
		Input.bShouldRagdollAfterDeathClip = Case.bShouldRagdollAfterDeathClip;
		Input.bShouldUseRagdollWithoutDeathClip = Case.bShouldUseRagdollWithoutDeathClip;
		TestTrue(FString::Printf(TEXT("%s resolves to presentation %d"), Case.Name, static_cast<int32>(Case.Expected)), TDDeathLaunchRules::ResolveDeathPresentation(Input) == Case.Expected);
	}

	FTDDeathLaunchInput ClipThenRagdollInput = MakeUprightDeathInput();
	ClipThenRagdollInput.Presentation = ETDDeathPresentation::ClipThenRagdoll;
	ClipThenRagdollInput.bWasCritical = true;
	const FTDDeathLaunch ClipThenRagdollLaunch = TDDeathLaunchRules::ComputeDeathLaunch(ClipThenRagdollInput);
	TestTrue(TEXT("A ragdoll after a death clip starts at rest"), ClipThenRagdollLaunch.LinearVelocity.IsZero() && ClipThenRagdollLaunch.AngularVelocityRadians.IsZero());
	TestFalse(TEXT("A ragdoll after a death clip does not watch for landing"), ClipThenRagdollLaunch.bWatchesLanding);
	TestTrue(TEXT("A ragdoll after a death clip adds no extra gravity"), ClipThenRagdollLaunch.ExtraGravity.IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchSlowMotionTest, "TDGame.Combat.DeathLaunch.SlowMotion.DeferAndStart", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchSlowMotionTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;

	struct FTDDeferCase
	{
		const TCHAR* Name = TEXT("");
		float GlobalTimeDilation = 1.f;
		bool bIsSlowMotionActive = false;
		bool bHasPendingSlowMotion = false;
		double PendingStartRealSeconds = 0.;
		double NowRealSeconds = 10.;
		float DeferredRealSeconds = 0.f;
		bool bShouldDefer = false;
	};
	const FTDDeferCase Cases[] = {
		{TEXT("Normal time"), 1.f, false, false, 0., 10., 0.f, false},
		{TEXT("A dilation of 0.94"), 0.94f, false, false, 0., 10., 0.f, true},
		{TEXT("A dilation of exactly 0.95"), 0.95f, false, false, 0., 10., 0.f, false},
		{TEXT("A phase slow motion of 0.5"), 0.5f, false, false, 0., 10., 0.f, true},
		{TEXT("A boss slow motion of 0.2"), 0.2f, false, false, 0., 10., 0.f, true},
		{TEXT("An active slow motion before the dilation applies"), 1.f, true, false, 0., 10., 0.f, true},
		{TEXT("A pending slow motion 0.02 seconds away without the ragdoll lead"), 1.f, false, true, 10.02, 10., 0.f, true},
		{TEXT("A pending slow motion that is already due"), 1.f, false, true, 9.99, 10., 0.f, true},
		{TEXT("A pending slow motion pushed behind this entry"), 1.f, false, true, 10.035, 10.012, 0.f, false},
		{TEXT("A pending slow motion pushed behind this entry on time"), 1.f, false, true, 10.035, 10., 0.f, false},
		{TEXT("A distant pending slow motion"), 1.f, false, true, 10.3, 10., 0.f, false},
		{TEXT("A slow motion that outlasts the defer limit"), 0.2f, true, false, 0., 11.5, 1.5f, false},
		{TEXT("A slow motion just inside the defer limit"), 0.2f, true, false, 0., 11.49, 1.49f, true}
	};
	for (const FTDDeferCase& Case : Cases)
	{
		FTDRagdollDeferInput Input;
		Input.NowRealSeconds = Case.NowRealSeconds;
		Input.ScheduledEntryRealSeconds = 10.;
		Input.GlobalTimeDilation = Case.GlobalTimeDilation;
		Input.bIsSlowMotionActive = Case.bIsSlowMotionActive;
		Input.DeferredRealSeconds = Case.DeferredRealSeconds;
		if (Case.bHasPendingSlowMotion)
		{
			Input.NextSlowMotionStartRealSeconds = Case.PendingStartRealSeconds;
		}
		TestTrue(FString::Printf(TEXT("%s %s the ragdoll start"), Case.Name, Case.bShouldDefer ? TEXT("defers") : TEXT("does not defer")), TDDeathLaunchRules::ShouldDeferRagdollStart(Input) == Case.bShouldDefer);
	}

	TestTrue(TEXT("Without a scheduled ragdoll the requested slow motion start is kept"), ResolveSlowMotionStart(10.05, TOptional<double>(), 10.05, false) == 10.05);
	TestTrue(TEXT("A slow motion requested before a ragdoll entry starts 0.035 seconds after it"), FMath::IsNearlyEqual(ResolveSlowMotionStart(10.05, TOptional<double>(10.097), 10.05, false), 10.132, 0.000001));
	TestTrue(TEXT("A slow motion requested well after the entry keeps its time"), FMath::IsNearlyEqual(ResolveSlowMotionStart(10.5, TOptional<double>(10.097), 10.5, false), 10.5, 0.000001));
	TestTrue(TEXT("The ragdoll lead never pushes a slow motion more than 0.3 seconds past its request"), FMath::IsNearlyEqual(ResolveSlowMotionStart(10., TOptional<double>(10.4), 10., false), 10.3, 0.000001));
	TestTrue(TEXT("A slow motion starts 0.035 seconds after the actual entry, not the scheduled one"), FMath::IsNearlyEqual(ResolveSlowMotionStart(10.05, TOptional<double>(10.13), 10.13, false), 10.165, 0.000001));

	bool bWaitingRagdollHoldsSlowMotion = true;
	bool bWaitingRagdollKeepsDelayCap = true;
	for (int32 NowStep = 0; NowStep <= 40; ++NowStep)
	{
		const double Request = 10.05;
		const double Now = Request + 0.01 * NowStep;
		const double Start = ResolveSlowMotionStart(Request, TOptional<double>(10.04), Now, true);
		if (Now + 0.035 < Request + 0.3 - 0.000001)
		{
			bWaitingRagdollHoldsSlowMotion = bWaitingRagdollHoldsSlowMotion && Start > Now;
		}
		bWaitingRagdollKeepsDelayCap = bWaitingRagdollKeepsDelayCap && Start <= Request + 0.3 + 0.000001;
	}
	TestTrue(TEXT("A ragdoll still waiting for its entry keeps a due slow motion from starting"), bWaitingRagdollHoldsSlowMotion);
	TestTrue(TEXT("A waiting ragdoll never holds a slow motion past 0.3 seconds after its request"), bWaitingRagdollKeepsDelayCap);
	TestTrue(TEXT("Without a waiting ragdoll a due slow motion starts at once"), ResolveSlowMotionStart(10.05, TOptional<double>(10.), 10.2, false) <= 10.2);

	bool bIsMonotonicInRequest = true;
	bool bIsMonotonicInEntry = true;
	bool bLeavesLead = true;
	bool bKeepsRequest = true;
	for (int32 EntryStep = 0; EntryStep <= 20; ++EntryStep)
	{
		double PreviousStart = -1.;
		for (int32 RequestStep = 0; RequestStep <= 20; ++RequestStep)
		{
			const double Entry = 10. + 0.01 * EntryStep;
			const double Request = 10. + 0.01 * RequestStep;
			const double Start = ResolveSlowMotionStart(Request, TOptional<double>(Entry), Request, false);
			const double LaterEntryStart = ResolveSlowMotionStart(Request, TOptional<double>(Entry + 0.01), Request, false);
			bIsMonotonicInRequest = bIsMonotonicInRequest && Start >= PreviousStart;
			bIsMonotonicInEntry = bIsMonotonicInEntry && LaterEntryStart >= Start;
			bLeavesLead = bLeavesLead && Start >= Entry + 0.035 - 0.000001;
			bKeepsRequest = bKeepsRequest && Start >= Request;
			PreviousStart = Start;
		}
	}
	TestTrue(TEXT("The slow motion start never decreases with a later request"), bIsMonotonicInRequest);
	TestTrue(TEXT("The slow motion start never decreases with a later ragdoll entry"), bIsMonotonicInEntry);
	TestTrue(TEXT("The slow motion always leaves two physics steps after the latest entry"), bLeavesLead);
	TestTrue(TEXT("The slow motion never starts before it was requested"), bKeepsRequest);

	bool bPushedSlowMotionNeverDefers = true;
	for (int32 LateStep = 0; LateStep <= 10; ++LateStep)
	{
		FTDRagdollDeferInput Input;
		Input.ScheduledEntryRealSeconds = 20.097;
		Input.NowRealSeconds = Input.ScheduledEntryRealSeconds + 0.01 * LateStep;
		Input.NextSlowMotionStartRealSeconds = ResolveSlowMotionStart(20.05, TOptional<double>(Input.ScheduledEntryRealSeconds), Input.NowRealSeconds, true);
		bPushedSlowMotionNeverDefers = bPushedSlowMotionNeverDefers && !TDDeathLaunchRules::ShouldDeferRagdollStart(Input);
	}
	TestTrue(TEXT("A slow motion held for a waiting ragdoll never defers that ragdoll, even when the entry runs several frames late"), bPushedSlowMotionNeverDefers);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchKillPoseHoldTest, "TDGame.Combat.DeathLaunch.KillPoseHold.Rules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchKillPoseHoldTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;

	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const auto ExpectHold = [](const float Seconds)
	{
		return FMath::Clamp(Seconds, 0.07f, 0.15f);
	};
	const float Light = FeedbackSettings.LightHitStopSeconds;
	const float Heavy = FeedbackSettings.HeavyHitStopSeconds;
	const float Critical = FeedbackSettings.CriticalHitStopBonusSeconds;
	const float Tolerance = 0.0001f;

	TestTrue(TEXT("A light normal kill holds the light victim stop, at least 0.07"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Normal, false), ExpectHold(Light), Tolerance));
	TestTrue(TEXT("A heavy normal kill holds the heavy victim stop"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Heavy, ETDKillRank::Normal, false), ExpectHold(Heavy), Tolerance));
	TestTrue(TEXT("A critical light kill adds the critical stop bonus"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Normal, true), ExpectHold(Light + Critical), Tolerance));
	TestTrue(TEXT("A critical heavy kill adds the critical stop bonus"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Heavy, ETDKillRank::Normal, true), ExpectHold(Heavy + Critical), Tolerance));
	TestTrue(TEXT("An elite kill adds 0.02"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Elite, false), ExpectHold(Light + 0.02f), Tolerance));
	TestTrue(TEXT("A boss kill adds 0.03"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Boss, false), ExpectHold(Light + 0.03f), Tolerance));
	TestTrue(TEXT("An unranked kill adds nothing"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Heavy, ETDKillRank::None, false), ExpectHold(Heavy), Tolerance));
	TestTrue(TEXT("A critical heavy boss kill stops at 0.15"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Heavy, ETDKillRank::Boss, true), ExpectHold(Heavy + Critical + 0.03f), Tolerance));

	bool bStaysInRange = true;
	for (const ETDHitFeelTier Tier : {ETDHitFeelTier::Light, ETDHitFeelTier::Heavy})
	{
		for (const ETDKillRank KillRank : {ETDKillRank::None, ETDKillRank::Normal, ETDKillRank::Elite, ETDKillRank::Boss})
		{
			for (const bool bWasCritical : {false, true})
			{
				const float Hold = ComputeKillHold(Tier, KillRank, bWasCritical);
				bStaysInRange = bStaysInRange && Hold >= 0.07f - Tolerance && Hold <= 0.15f + Tolerance;
			}
		}
	}
	TestTrue(TEXT("Every kill pose hold stays between 0.07 and 0.15 seconds before the wave"), bStaysInRange);

	const float NearHold = ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Normal, false);
	TestTrue(TEXT("A ground area kill 200 cm from its center waits 0.05 seconds longer"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Normal, false, 200.f, true) - NearHold, 0.05f, Tolerance));
	TestTrue(TEXT("A ground area kill 400 cm from its center waits 0.1 seconds longer"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Normal, false, 400.f, true) - NearHold, 0.1f, Tolerance));
	TestTrue(TEXT("The ground area wave stops at 0.1 seconds"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Normal, false, 1200.f, true) - NearHold, 0.1f, Tolerance));
	TestTrue(TEXT("A ground area kill at its center adds no wave delay"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Normal, false, 0.f, true), NearHold, Tolerance));
	TestTrue(TEXT("A negative ground area distance adds nothing"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Normal, false, -50.f, true), NearHold, Tolerance));
	TestTrue(TEXT("The ground area wave is added after the hold clamp"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Heavy, ETDKillRank::Boss, true, 400.f, true), ExpectHold(Heavy + Critical + 0.03f) + 0.1f, Tolerance));
	TestTrue(TEXT("A distance without a ground area kill adds no wave delay"), FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Normal, false, 400.f, false), NearHold, Tolerance));

	float PreviousHold = 0.f;
	bool bWaveIsMonotonic = true;
	for (int32 Step = 0; Step <= 12; ++Step)
	{
		const float Hold = ComputeKillHold(ETDHitFeelTier::Heavy, ETDKillRank::Elite, false, 50.f * Step, true);
		bWaveIsMonotonic = bWaveIsMonotonic && Hold >= PreviousHold;
		PreviousHold = Hold;
	}
	TestTrue(TEXT("Ground area kills farther from the center never end their hold earlier"), bWaveIsMonotonic);

	bool bFirstKillHasNoStagger = true;
	bool bStaggerStaysInRange = true;
	bool bStaggerRepeats = true;
	bool bHoldAddsStagger = true;
	bool bGroundAreaIgnoresKillIndex = true;
	float MinStaggerSeconds = TNumericLimits<float>::Max();
	float MaxStaggerSeconds = 0.f;
	for (uint32 Seed = 1; Seed <= SeedCount; ++Seed)
	{
		bFirstKillHasNoStagger = bFirstKillHasNoStagger && TDDeathLaunchRules::ComputeMultiKillStaggerSeconds(Seed, 0) == 0.f && TDDeathLaunchRules::ComputeMultiKillStaggerSeconds(Seed, -1) == 0.f;
		bFirstKillHasNoStagger = bFirstKillHasNoStagger && FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Normal, false, 0.f, false, 0, Seed), NearHold, Tolerance);
		bGroundAreaIgnoresKillIndex = bGroundAreaIgnoresKillIndex && FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Normal, false, 200.f, true, 3, Seed), ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Normal, false, 200.f, true, 0, Seed), Tolerance);
		for (int32 KillIndex = 1; KillIndex <= 4; ++KillIndex)
		{
			const float StaggerSeconds = TDDeathLaunchRules::ComputeMultiKillStaggerSeconds(Seed, KillIndex);
			MinStaggerSeconds = FMath::Min(MinStaggerSeconds, StaggerSeconds);
			MaxStaggerSeconds = FMath::Max(MaxStaggerSeconds, StaggerSeconds);
			bStaggerStaysInRange = bStaggerStaysInRange && StaggerSeconds >= 0.02f - Tolerance && StaggerSeconds <= 0.07f;
			bStaggerRepeats = bStaggerRepeats && StaggerSeconds == TDDeathLaunchRules::ComputeMultiKillStaggerSeconds(Seed, KillIndex);
			bHoldAddsStagger = bHoldAddsStagger && FMath::IsNearlyEqual(ComputeKillHold(ETDHitFeelTier::Light, ETDKillRank::Normal, false, 0.f, false, KillIndex, Seed) - NearHold, StaggerSeconds, Tolerance);
		}
	}
	TestTrue(TEXT("The first kill of a multi kill window adds no stagger"), bFirstKillHasNoStagger);
	TestTrue(FString::Printf(TEXT("A later kill of a multi kill window waits 0.02 to under 0.07 seconds longer (seen %.4f to %.4f)"), MinStaggerSeconds, MaxStaggerSeconds), bStaggerStaysInRange);
	TestTrue(TEXT("The multi kill stagger repeats for the same seed"), bStaggerRepeats);
	TestTrue(TEXT("The kill pose hold adds the multi kill stagger after the hold clamp"), bHoldAddsStagger);
	TestTrue(TEXT("Different victims spread their stagger across the range"), MaxStaggerSeconds - MinStaggerSeconds > 0.03f);
	TestTrue(TEXT("A ground area kill keeps its center distance wave whatever its multi kill index"), bGroundAreaIgnoresKillIndex);

	FTDKillPoseHoldInput PlayerInput;
	PlayerInput.bIsPlayer = true;
	PlayerInput.Tier = ETDHitFeelTier::Heavy;
	PlayerInput.bWasCritical = true;
	PlayerInput.GroundAreaDistance = 400.f;
	PlayerInput.MultiKillIndex = 3;
	PlayerInput.bIsGroundAreaKill = true;
	TestTrue(TEXT("A player death holds 0.1 seconds"), FMath::IsNearlyEqual(TDDeathLaunchRules::ComputeKillPoseHoldRealSeconds(PlayerInput), 0.1f, Tolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchVerticalSpinCapTest, "TDGame.Combat.DeathLaunch.Rotation.VerticalSpinCap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchVerticalSpinCapTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;
	constexpr float SpinToleranceDegrees = 0.01f;

	const uint32 StrongestSeed = FindStrongestVerticalSpinSeed();
	const FTDDeathLaunchVariation StrongestVariation = TDDeathLaunchRules::MakeDeathLaunchVariation(StrongestSeed);
	TestTrue(TEXT("The strongest seed tilts the spin axis past 24 degrees"), FMath::Abs(StrongestVariation.AxisTiltDegrees) > 24.f);
	TestTrue(TEXT("The strongest seed yaws in the same direction as its tilt"), FMath::Sign(StrongestVariation.AxisTiltDegrees) == FMath::Sign(StrongestVariation.YawDegreesPerSecond));

	TArray<uint32> Seeds;
	for (uint32 Seed = 0; Seed <= SeedCount; ++Seed)
	{
		Seeds.Add(Seed);
	}
	Seeds.Add(StrongestSeed);

	bool bStaysUnderCap = true;
	bool bMatchesCappedTilt = true;
	float MinShortDropTipDegrees = TNumericLimits<float>::Max();
	for (const uint32 Seed : Seeds)
	{
		const FTDDeathLaunchInput NormalInput = MakeUprightDeathInput(90.f, Seed);
		FTDDeathLaunchInput ShortDropInput = MakeUprightDeathInput(90.f, Seed);
		ShortDropInput.CenterOfMassHeight = 0.f;
		FTDDeathLaunchInput ShortChopInput = ShortDropInput;
		ShortChopInput.SwingDirection = FVector(0.f, 0.f, -1.f);
		FTDDeathLaunchInput ShortStatusInput = ShortDropInput;
		ShortStatusInput.Delivery = ETDDamageDelivery::Status;
		FTDDeathLaunchInput ShortHorizontalInput = MakeHorizontalDeathInput(60.f, Seed);
		ShortHorizontalInput.CenterOfMassHeight = 0.f;
		for (const FTDDeathLaunchInput& Input : {NormalInput, ShortDropInput, ShortChopInput, ShortStatusInput, ShortHorizontalInput})
		{
			const FTDDeathLaunch Launch = TDDeathLaunchRules::ComputeDeathLaunch(Input);
			const float VerticalSpinDegrees = MeasureLaunchVerticalSpinDegreesPerSecond(Launch);
			bStaysUnderCap = bStaysUnderCap && FMath::Abs(VerticalSpinDegrees) <= MaxVerticalSpinDegreesPerSecond + SpinToleranceDegrees;
			bMatchesCappedTilt = bMatchesCappedTilt && FMath::IsNearlyEqual(VerticalSpinDegrees, ComputeExpectedVerticalSpinDegreesPerSecond(Input, Launch), SpinToleranceDegrees);
			if (Input.CenterOfMassHeight <= 0.f)
			{
				MinShortDropTipDegrees = FMath::Min(MinShortDropTipDegrees, MeasureLaunchTipDegreesPerSecond(Launch));
			}
		}
	}
	TestTrue(TEXT("Every short drop tips faster than the 360 degrees per second tilt limit"), MinShortDropTipDegrees > MaxTiltTipDegreesPerSecond);
	TestTrue(TEXT("The vertical spin never passes 300 degrees per second"), bStaysUnderCap);
	TestTrue(TEXT("The vertical spin is the capped tilt term plus yaw"), bMatchesCappedTilt);

	FTDDeathLaunchInput StrongestShortStatusInput = MakeUprightDeathInput(90.f, StrongestSeed);
	StrongestShortStatusInput.CenterOfMassHeight = 0.f;
	StrongestShortStatusInput.Delivery = ETDDamageDelivery::Status;
	const FTDDeathLaunch StrongestLaunch = TDDeathLaunchRules::ComputeDeathLaunch(StrongestShortStatusInput);
	TestTrue(TEXT("The strongest seed on a short drop tips past the tilt limit"), MeasureLaunchTipDegreesPerSecond(StrongestLaunch) > MaxTiltTipDegreesPerSecond);
	TestTrue(TEXT("The strongest seed on a short drop is held at 300 degrees per second"), FMath::IsNearlyEqual(FMath::Abs(MeasureLaunchVerticalSpinDegreesPerSecond(StrongestLaunch)), MaxVerticalSpinDegreesPerSecond, SpinToleranceDegrees));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchBiteDampingTest, "TDGame.Combat.DeathLaunch.Landing.BiteDamping", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchBiteDampingTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("An upright corpse bites the ground with angular damping 3"), FMath::IsNearlyEqual(TDDeathLaunchRules::ComputeBiteAngularDamping(false), 3.f));
	TestTrue(TEXT("A horizontal corpse bites the ground with angular damping 6"), FMath::IsNearlyEqual(TDDeathLaunchRules::ComputeBiteAngularDamping(true), 6.f));
	TestTrue(TEXT("A landing bite uses linear damping 5"), FMath::IsNearlyEqual(TDDeathLaunchRules::BiteLinearDamping, 5.f));
	TestTrue(TEXT("A landing bite lasts 0.35 seconds"), FMath::IsNearlyEqual(TDDeathLaunchRules::BiteSeconds, 0.35f));
	TestTrue(TEXT("A landing bite damps harder than the launch"), TDDeathLaunchRules::BiteLinearDamping > TDDeathLaunchRules::LaunchLinearDamping && TDDeathLaunchRules::ComputeBiteAngularDamping(false) > TDDeathLaunchRules::LaunchAngularDamping);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchLandingRollLimitTest, "TDGame.Combat.DeathLaunch.Landing.RollLimit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchLandingRollLimitTest::RunTest(const FString& Parameters)
{
	constexpr float ToleranceDegreesPerSecond = 0.01f;
	const auto ComputeExcessDegrees = [](const float RollDegreesPerSecond)
	{
		return FMath::RadiansToDegrees(TDDeathLaunchRules::ComputeLandingRollSpeedExcess(FMath::DegreesToRadians(RollDegreesPerSecond)));
	};

	TestTrue(TEXT("A landing roll is capped at 200 degrees per second"), FMath::IsNearlyEqual(TDDeathLaunchRules::MaxLandingRollDegreesPerSecond, 200.f));
	TestTrue(TEXT("A roll at or under the cap keeps all of its speed"), FMath::IsNearlyZero(ComputeExcessDegrees(0.f)) && FMath::IsNearlyZero(ComputeExcessDegrees(180.f)) && FMath::IsNearlyZero(ComputeExcessDegrees(-200.f), ToleranceDegreesPerSecond));
	TestTrue(TEXT("A 640 degrees per second trip roll loses its 440 degrees per second excess"), FMath::IsNearlyEqual(ComputeExcessDegrees(640.f), 440.f, ToleranceDegreesPerSecond));
	TestTrue(TEXT("A reverse 500 degrees per second roll loses a reverse 300 degrees per second excess"), FMath::IsNearlyEqual(ComputeExcessDegrees(-500.f), -300.f, ToleranceDegreesPerSecond));
	TestTrue(TEXT("Removing the excess leaves the roll exactly at the cap"), FMath::IsNearlyEqual(520.f - ComputeExcessDegrees(520.f), 200.f, ToleranceDegreesPerSecond));

	const TArray<FName> TorsoBones = { FName(TEXT("BARGHEST_-Pelvis")), FName(TEXT("BARGHEST_-Spine3")), FName(TEXT("bip01_pelvis")), FName(TEXT("bip01_spine2")), FName(TEXT("CENTAUR_-Spine2")), FName(TEXT("GRIFFON_-Spine1")), FName(TEXT("MOUNTAIN_DRAGON_-Pelvis")) };
	const TArray<FName> LimbBones = { FName(TEXT("GRIFFON_WingUpperArm_R")), FName(TEXT("MOUNTAIN_DRAGON_WING_L_FINGER_C_3")), FName(TEXT("BARGHEST_-Neck")), FName(TEXT("bip01_head")), FName(TEXT("CENTAUR_-L-Thigh")), FName(TEXT("BARGHEST_-Tail")) };
	TestFalse(TEXT("No pelvis or spine body of a horizontal rig is left out of the torso roll"), TorsoBones.ContainsByPredicate([](const FName BoneName) { return !TDDeathLaunchRules::IsLandingRollTorsoBone(BoneName); }));
	TestFalse(TEXT("No wing, neck, head, leg or tail body joins the torso roll"), LimbBones.ContainsByPredicate([](const FName BoneName) { return TDDeathLaunchRules::IsLandingRollTorsoBone(BoneName); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchLandingRollLimitScopeTest, "TDGame.Combat.DeathLaunch.Landing.RollLimitScope", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchLandingRollLimitScopeTest::RunTest(const FString& Parameters)
{
	struct FTDRollLimitCase
	{
		const TCHAR* Name = TEXT("");
		bool bHasHorizontalBodyPlan = true;
		float CapsuleHalfHeight = 48.f;
		float TorsoMassRatio = 0.4f;
		bool bShouldLimitRoll = false;
	};
	const FTDRollLimitCase Cases[] = {
		{TEXT("Hyena"), true, 48.f, 0.43f, true},
		{TEXT("Barghest"), true, 48.f, 0.42f, true},
		{TEXT("BarghestAlpha"), true, 57.f, 0.42f, true},
		{TEXT("A horizontal body exactly at half height 70 and torso mass ratio 0.3"), true, 70.f, 0.3f, true},
		{TEXT("A horizontal body just over half height 70"), true, 70.1f, 0.42f, false},
		{TEXT("A horizontal body just under torso mass ratio 0.3"), true, 48.f, 0.299f, false},
		{TEXT("A small winged body with light torso bodies"), true, 48.f, 0.07f, false},
		{TEXT("Griffon"), true, 95.f, 0.06f, false},
		{TEXT("Centaur"), true, 108.f, 0.35f, false},
		{TEXT("GriffonAlpha"), true, 114.f, 0.07f, false},
		{TEXT("CentaurHunter"), true, 122.f, 0.35f, false},
		{TEXT("MountainDragon"), true, 160.f, 0.08f, false},
		{TEXT("A small upright body"), false, 48.f, 0.43f, false}
	};

	TestTrue(TEXT("The landing roll limit stops at capsule half height 70"), FMath::IsNearlyEqual(TDDeathLaunchRules::MaxLandingRollCapsuleHalfHeight, 70.f));
	TestTrue(TEXT("The landing roll limit needs at least 0.3 of the mass in torso bodies"), FMath::IsNearlyEqual(TDDeathLaunchRules::MinLandingRollTorsoMassRatio, 0.3f));
	for (const FTDRollLimitCase& Case : Cases)
	{
		const bool bLimitsRoll = TDDeathLaunchRules::ShouldLimitLandingRoll(Case.bHasHorizontalBodyPlan, Case.CapsuleHalfHeight, Case.TorsoMassRatio);
		TestTrue(FString::Printf(TEXT("%s %s the landing roll limit"), Case.Name, Case.bShouldLimitRoll ? TEXT("gets") : TEXT("skips")), bLimitsRoll == Case.bShouldLimitRoll);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchVariationTest, "TDGame.Combat.DeathLaunch.Variation.Deterministic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchVariationTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	FTDDeathLaunchInput Input = MakeUprightDeathInput(90.f, 4242);
	Input.SwingDirection = MakeDeathPlanarDirection(60.f);
	const FTDDeathLaunch FirstLaunch = TDDeathLaunchRules::ComputeDeathLaunch(Input);
	const FTDDeathLaunch SecondLaunch = TDDeathLaunchRules::ComputeDeathLaunch(Input);
	TestTrue(TEXT("The same seed gives the same launch velocity"), FirstLaunch.LinearVelocity == SecondLaunch.LinearVelocity);
	TestTrue(TEXT("The same seed gives the same spin"), FirstLaunch.AngularVelocityRadians == SecondLaunch.AngularVelocityRadians);

	bool bStaysInRange = true;
	bool bHasBothDirectionSigns = false;
	bool bHasBothYawSigns = false;
	bool bHasBothSides = false;
	float MinDirection = TNumericLimits<float>::Max();
	float MaxDirection = TNumericLimits<float>::Lowest();
	float MinDistance = TNumericLimits<float>::Max();
	float MaxDistance = TNumericLimits<float>::Lowest();
	float MaxAxisTilt = 0.f;
	float MinYawSpeed = TNumericLimits<float>::Max();
	float MaxYawSpeed = 0.f;
	int32 DistinctNeighborCount = 0;
	int32 DistinctLaunchCount = 0;
	FTDDeathLaunchVariation PreviousVariation = TDDeathLaunchRules::MakeDeathLaunchVariation(0);
	FTDDeathLaunch PreviousLaunch = TDDeathLaunchRules::ComputeDeathLaunch(MakeUprightDeathInput(90.f, 0));
	const FTDDeathLaunchVariation FirstVariation = TDDeathLaunchRules::MakeDeathLaunchVariation(1);
	for (uint32 Seed = 1; Seed <= SeedCount; ++Seed)
	{
		const FTDDeathLaunchVariation Variation = TDDeathLaunchRules::MakeDeathLaunchVariation(Seed);
		const FTDDeathLaunchVariation RepeatedVariation = TDDeathLaunchRules::MakeDeathLaunchVariation(Seed);
		bStaysInRange = bStaysInRange && Variation.DirectionDegrees == RepeatedVariation.DirectionDegrees && Variation.DistanceScale == RepeatedVariation.DistanceScale;
		bStaysInRange = bStaysInRange && FMath::Abs(Variation.DirectionDegrees) <= 8.f;
		bStaysInRange = bStaysInRange && Variation.DistanceScale >= 0.92f && Variation.DistanceScale <= 1.08f;
		bStaysInRange = bStaysInRange && Variation.SpinScale >= 0.92f && Variation.SpinScale <= 1.08f;
		bStaysInRange = bStaysInRange && FMath::Abs(Variation.AxisTiltDegrees) <= 25.f;
		bStaysInRange = bStaysInRange && FMath::Abs(Variation.YawDegreesPerSecond) >= 30.f && FMath::Abs(Variation.YawDegreesPerSecond) <= 150.f;
		MaxAxisTilt = FMath::Max(MaxAxisTilt, FMath::Abs(Variation.AxisTiltDegrees));
		MinYawSpeed = FMath::Min(MinYawSpeed, FMath::Abs(Variation.YawDegreesPerSecond));
		MaxYawSpeed = FMath::Max(MaxYawSpeed, FMath::Abs(Variation.YawDegreesPerSecond));
		bStaysInRange = bStaysInRange && FMath::Abs(Variation.SideSign) == 1.f;
		bHasBothDirectionSigns = bHasBothDirectionSigns || FMath::Sign(Variation.DirectionDegrees) != FMath::Sign(FirstVariation.DirectionDegrees);
		bHasBothYawSigns = bHasBothYawSigns || FMath::Sign(Variation.YawDegreesPerSecond) != FMath::Sign(FirstVariation.YawDegreesPerSecond);
		bHasBothSides = bHasBothSides || Variation.SideSign != FirstVariation.SideSign;
		MinDirection = FMath::Min(MinDirection, Variation.DirectionDegrees);
		MaxDirection = FMath::Max(MaxDirection, Variation.DirectionDegrees);
		MinDistance = FMath::Min(MinDistance, Variation.DistanceScale);
		MaxDistance = FMath::Max(MaxDistance, Variation.DistanceScale);
		DistinctNeighborCount += FMath::Abs(Variation.DirectionDegrees - PreviousVariation.DirectionDegrees) > 2.f ? 1 : 0;
		PreviousVariation = Variation;

		const FTDDeathLaunch Launch = TDDeathLaunchRules::ComputeDeathLaunch(MakeUprightDeathInput(90.f, Seed));
		DistinctLaunchCount += Launch.LinearVelocity.Equals(PreviousLaunch.LinearVelocity, 0.01) ? 0 : 1;
		PreviousLaunch = Launch;
	}
	TestTrue(TEXT("Every variation is repeatable and inside its range"), bStaysInRange);
	TestTrue(TEXT("Direction variation goes both ways"), bHasBothDirectionSigns);
	TestTrue(TEXT("Yaw spin goes both ways"), bHasBothYawSigns);
	TestTrue(TEXT("Front-hit roll side goes both ways"), bHasBothSides);
	TestTrue(TEXT("Direction variation spreads across most of its range"), MaxDirection - MinDirection > 12.f);
	TestTrue(TEXT("Distance variation spreads across most of its range"), MaxDistance - MinDistance > 0.12f);
	TestTrue(TEXT("Axis tilt reaches past 20 degrees"), MaxAxisTilt > 20.f);
	TestTrue(TEXT("Yaw spin speed spreads across most of 30 to 150 degrees per second"), MinYawSpeed < 45.f && MaxYawSpeed > 135.f);
	TestTrue(TEXT("Consecutive seeds such as neighboring actor ids are not correlated"), DistinctNeighborCount >= static_cast<int32>(SeedCount) / 2);
	TestTrue(TEXT("Consecutive seeds give different launches"), DistinctLaunchCount >= static_cast<int32>(SeedCount) - 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchLandingTest, "TDGame.Combat.DeathLaunch.Landing.DetectionAndSound", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchLandingTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	const FTDDeathLaunch Launch = TDDeathLaunchRules::ComputeDeathLaunch(MakeUprightDeathInput(90.f));
	const float LowHeight = Launch.GroundHeight + Launch.RestHeight + 20.f;
	const float HighHeight = Launch.GroundHeight + Launch.RestHeight + 30.f;
	const auto MakeSample = [](const float VerticalSpeed, const float MinVerticalSpeed, const float CenterOfMassHeight, const float ElapsedSeconds)
	{
		FTDDeathLandingSample Sample;
		Sample.VerticalSpeed = VerticalSpeed;
		Sample.MinVerticalSpeed = MinVerticalSpeed;
		Sample.CenterOfMassHeight = CenterOfMassHeight;
		Sample.ElapsedSeconds = ElapsedSeconds;
		return Sample;
	};

	const float FallbackHeight = Launch.GroundHeight + Launch.RestHeight + 74.f;
	const float AboveFallbackHeight = Launch.GroundHeight + Launch.RestHeight + 76.f;
	const float FallbackSeconds = Launch.ExpectedFallSeconds + 0.25f;
	const auto ResolveState = [&Launch](const FTDDeathLandingSample& Sample)
	{
		return TDDeathLaunchRules::ResolveDeathLandingState(Launch, Sample);
	};

	TestTrue(TEXT("A body still falling has not landed"), ResolveState(MakeSample(-300.f, -300.f, LowHeight, 0.2f)) == ETDDeathLandingState::Falling);
	TestTrue(TEXT("A recovery under 25 percent of a fast fall is not a landing"), ResolveState(MakeSample(-301.f, -400.f, LowHeight, 0.2f)) == ETDDeathLandingState::Falling);
	TestTrue(TEXT("A recovery of 25 percent of the fall speed near the ground is a landing"), ResolveState(MakeSample(-300.f, -400.f, LowHeight, 0.2f)) == ETDDeathLandingState::Landed);
	TestTrue(TEXT("A recovery high above the rest height is not a landing"), ResolveState(MakeSample(-200.f, -400.f, HighHeight, 0.2f)) == ETDDeathLandingState::Falling);
	TestTrue(TEXT("A fall slower than 60 is not a landing"), ResolveState(MakeSample(20.f, -50.f, LowHeight, 0.2f)) == ETDDeathLandingState::Falling);
	TestTrue(TEXT("A recovery under 35 is not a landing"), ResolveState(MakeSample(-70.f, -100.f, LowHeight, 0.2f)) == ETDDeathLandingState::Falling);
	TestTrue(TEXT("A recovery of 35 from a slow fall is a landing"), ResolveState(MakeSample(-65.f, -100.f, LowHeight, 0.2f)) == ETDDeathLandingState::Landed);
	TestTrue(TEXT("The fallback does not fire before the expected fall time plus 0.25"), ResolveState(MakeSample(0.f, 0.f, HighHeight, Launch.ExpectedFallSeconds + 0.24f)) == ETDDeathLandingState::Falling);
	TestTrue(TEXT("The fallback fires at the expected fall time plus 0.25"), ResolveState(MakeSample(0.f, 0.f, HighHeight, FallbackSeconds)) == ETDDeathLandingState::Landed);
	TestTrue(TEXT("The fallback fires within 75 above the rest height"), ResolveState(MakeSample(0.f, 0.f, FallbackHeight, FallbackSeconds)) == ETDDeathLandingState::Landed);
	TestTrue(TEXT("The fallback waits while the body is more than 75 above the rest height"), ResolveState(MakeSample(0.f, 0.f, AboveFallbackHeight, FallbackSeconds)) == ETDDeathLandingState::Falling);
	TestTrue(TEXT("A body high in the air has not landed just before 1.5 seconds"), ResolveState(MakeSample(0.f, 0.f, Launch.GroundHeight + 500.f, 1.49f)) == ETDDeathLandingState::Falling);
	TestTrue(TEXT("A body high in the air at 1.5 seconds is overdue, not landed"), ResolveState(MakeSample(0.f, 0.f, Launch.GroundHeight + 500.f, 1.5f)) == ETDDeathLandingState::Overdue);
	TestTrue(TEXT("An overdue body more than 75 above the rest height keeps waiting"), ResolveState(MakeSample(0.f, 0.f, AboveFallbackHeight, 2.f)) == ETDDeathLandingState::Overdue);
	TestTrue(TEXT("An overdue body within 75 above the rest height lands"), ResolveState(MakeSample(0.f, 0.f, FallbackHeight, 1.5f)) == ETDDeathLandingState::Landed);
	TestTrue(TEXT("An overdue body that comes down before 3 seconds lands"), ResolveState(MakeSample(-400.f, -400.f, FallbackHeight, 2.99f)) == ETDDeathLandingState::Landed);
	TestTrue(TEXT("A body still high in the air at 3 seconds is abandoned without a landing"), ResolveState(MakeSample(0.f, 0.f, Launch.GroundHeight + 500.f, 3.f)) == ETDDeathLandingState::Abandoned);
	TestTrue(TEXT("The watch gives up at 3 seconds even near the ground"), ResolveState(MakeSample(0.f, 0.f, FallbackHeight, 3.f)) == ETDDeathLandingState::Abandoned);

	const float ExpectedImpactSpeed = static_cast<float>(Launch.LinearVelocity.Z) + FMath::Sqrt(2.f * EffectiveGravity * Launch.DropHeight);
	const FTDCorpseLandSound FullSound = TDDeathLaunchRules::ComputeCorpseLandSound(Launch, MakeSample(0.f, -ExpectedImpactSpeed, LowHeight, 0.3f), 90.f);
	TestTrue(TEXT("A full impact plays at full volume"), FMath::IsNearlyEqual(FullSound.VolumeScale, 1.f, 0.001f) && FMath::IsNearlyEqual(FullSound.PitchScale, 1.f, 0.001f));
	const FTDCorpseLandSound HalfSound = TDDeathLaunchRules::ComputeCorpseLandSound(Launch, MakeSample(0.f, -0.5f * ExpectedImpactSpeed, LowHeight, 0.3f), 90.f);
	TestTrue(TEXT("A half impact plays at half volume"), FMath::IsNearlyEqual(HalfSound.VolumeScale, 0.5f, 0.001f));
	const FTDCorpseLandSound SoftSound = TDDeathLaunchRules::ComputeCorpseLandSound(Launch, MakeSample(-60.f, -70.f, LowHeight, 0.3f), 90.f);
	TestTrue(TEXT("A soft impact plays at least at 0.3"), FMath::IsNearlyEqual(SoftSound.VolumeScale, 0.3f, 0.001f));
	const FTDCorpseLandSound HardSound = TDDeathLaunchRules::ComputeCorpseLandSound(Launch, MakeSample(100.f, -3.f * ExpectedImpactSpeed, LowHeight, 0.3f), 90.f);
	TestTrue(TEXT("A hard impact never plays above full volume"), FMath::IsNearlyEqual(HardSound.VolumeScale, 1.f, 0.001f));
	const FTDCorpseLandSound DetectedSound = TDDeathLaunchRules::ComputeCorpseLandSound(Launch, MakeSample(-0.45f * ExpectedImpactSpeed, -0.6f * ExpectedImpactSpeed, LowHeight, 0.3f), 90.f);
	TestTrue(TEXT("The volume follows the peak fall speed, not the rebound left at detection"), FMath::IsNearlyEqual(DetectedSound.VolumeScale, 0.6f, 0.001f));
	const FTDCorpseLandSound RisingSound = TDDeathLaunchRules::ComputeCorpseLandSound(Launch, MakeSample(80.f, 40.f, HighHeight, 1.5f), 90.f);
	TestTrue(TEXT("A forced landing that never fell plays at 0.3"), FMath::IsNearlyEqual(RisingSound.VolumeScale, 0.3f, 0.001f));

	const FTDDeathLandingSample FullSample = MakeSample(0.f, -ExpectedImpactSpeed, LowHeight, 0.3f);
	const FTDCorpseLandSound SmallSound = TDDeathLaunchRules::ComputeCorpseLandSound(Launch, FullSample, 40.f);
	TestTrue(TEXT("A small body lands at 0.7 volume and 1.1 pitch"), FMath::IsNearlyEqual(SmallSound.VolumeScale, 0.7f, 0.001f) && FMath::IsNearlyEqual(SmallSound.PitchScale, 1.1f, 0.001f));
	const FTDCorpseLandSound MediumSound = TDDeathLaunchRules::ComputeCorpseLandSound(Launch, FullSample, 60.f);
	TestTrue(TEXT("A body of half height 60 lands as a medium body"), FMath::IsNearlyEqual(MediumSound.VolumeScale, 1.f, 0.001f) && FMath::IsNearlyEqual(MediumSound.PitchScale, 1.f, 0.001f));
	const FTDCorpseLandSound LargeSound = TDDeathLaunchRules::ComputeCorpseLandSound(Launch, FullSample, 100.f);
	TestTrue(TEXT("A body of half height 100 lands at 1.3 volume and 0.85 pitch"), FMath::IsNearlyEqual(LargeSound.VolumeScale, 1.3f, 0.001f) && FMath::IsNearlyEqual(LargeSound.PitchScale, 0.85f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchCorpseTraumaRuleTest, "TDGame.Combat.DeathLaunch.Landing.CorpseTraumaRule", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchCorpseTraumaRuleTest::RunTest(const FString& Parameters)
{
	struct FTDTraumaCase
	{
		const TCHAR* Name = TEXT("");
		ETDKillRank KillRank = ETDKillRank::Normal;
		float CapsuleHalfHeight = 90.f;
		float ViewTargetDistance = 0.f;
		bool bShouldAddTrauma = false;
	};
	const FTDTraumaCase Cases[] = {
		{TEXT("A nearby normal body of half height 90"), ETDKillRank::Normal, 90.f, 300.f, false},
		{TEXT("A nearby unranked body just under half height 100"), ETDKillRank::None, 99.9f, 300.f, false},
		{TEXT("A nearby normal body of half height 100"), ETDKillRank::Normal, 100.f, 300.f, true},
		{TEXT("A nearby small elite"), ETDKillRank::Elite, 40.f, 300.f, true},
		{TEXT("A nearby small boss"), ETDKillRank::Boss, 40.f, 300.f, true},
		{TEXT("An elite exactly 1500 cm from the camera target"), ETDKillRank::Elite, 60.f, 1500.f, true},
		{TEXT("An elite just over 1500 cm from the camera target"), ETDKillRank::Elite, 60.f, 1500.1f, false},
		{TEXT("A large normal body 2000 cm from the camera target"), ETDKillRank::Normal, 160.f, 2000.f, false}
	};
	for (const FTDTraumaCase& Case : Cases)
	{
		const bool bAddsTrauma = TDDeathLaunchRules::ShouldAddCorpseLandTrauma(Case.KillRank, Case.CapsuleHalfHeight, Case.ViewTargetDistance);
		TestTrue(FString::Printf(TEXT("%s %s landing trauma"), Case.Name, Case.bShouldAddTrauma ? TEXT("adds") : TEXT("skips")), bAddsTrauma == Case.bShouldAddTrauma);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchHorizontalLandingCapTest, "TDGame.Combat.DeathLaunch.Rotation.HorizontalLandingCap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchHorizontalLandingCapTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	const float HalfHeights[] = {40.f, 60.f, 90.f, 130.f};
	const ETDDamageDelivery Deliveries[] = {ETDDamageDelivery::Melee, ETDDamageDelivery::Spell, ETDDamageDelivery::Direct, ETDDamageDelivery::Status};
	const FVector Facings[] = {-FVector::ForwardVector, FVector::RightVector};
	bool bStaysUnderCap = true;
	bool bIgnoresLaunchSpeed = true;
	float MaxLandingDegrees = 0.f;
	for (uint32 Seed = 1; Seed <= SeedCount; ++Seed)
	{
		for (const float HalfHeight : HalfHeights)
		{
			for (const ETDDamageDelivery Delivery : Deliveries)
			{
				for (const FVector& Facing : Facings)
				{
					FTDDeathLaunchInput WeakInput = MakeHorizontalDeathInput(HalfHeight, Seed);
					WeakInput.Delivery = Delivery;
					WeakInput.ActorForward = Facing;
					WeakInput.SwingDirection = MakeDeathPlanarDirection(50.f);
					WeakInput.DamageRatio = 0.f;
					WeakInput.UnclampedDamage = 0.f;
					const float WeakLandingDegrees = MeasureLaunchLandingDegrees(TDDeathLaunchRules::ComputeDeathLaunch(WeakInput));
					for (int32 Strength = 0; Strength < 3; ++Strength)
					{
						FTDDeathLaunchInput Input = WeakInput;
						Input.DamageRatio = 0.5f * Strength;
						Input.bWasCritical = Strength > 0;
						Input.bIsComboFinisher = Strength > 1;
						const float LandingDegrees = MeasureLaunchLandingDegrees(TDDeathLaunchRules::ComputeDeathLaunch(Input));
						MaxLandingDegrees = FMath::Max(MaxLandingDegrees, LandingDegrees);
						bStaysUnderCap = bStaysUnderCap && LandingDegrees <= MaxHorizontalLandingDegrees + 0.01f;
						bIgnoresLaunchSpeed = bIgnoresLaunchSpeed && FMath::IsNearlyEqual(LandingDegrees, WeakLandingDegrees, AngleToleranceDegrees);
					}
				}
			}
		}
	}
	TestTrue(FString::Printf(TEXT("A horizontal body's predicted landing angle never exceeds 95 degrees for any seed or strength (seen up to %.1f)"), MaxLandingDegrees), bStaysUnderCap);
	TestTrue(TEXT("A horizontal body's predicted landing angle does not grow with the launch speed"), bIgnoresLaunchSpeed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchStartsAtRestTest, "TDGame.Combat.DeathLaunch.Launch.StartsAtRest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchStartsAtRestTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathLaunchTestsInternal;
	const FTDDeathConsoleDefaults ConsoleDefaults;

	FTDDeathLaunchInput Input = MakeUprightDeathInput(90.f);
	Input.bWasCritical = true;
	Input.bIsComboFinisher = true;
	const FTDDeathLaunch MovingLaunch = TDDeathLaunchRules::ComputeDeathLaunch(Input);
	Input.bStartsAtRest = true;
	const FTDDeathLaunch RestingLaunch = TDDeathLaunchRules::ComputeDeathLaunch(Input);
	TestTrue(TEXT("The same kill launches the body when it does not start at rest"), !MovingLaunch.LinearVelocity.IsNearlyZero());
	TestTrue(TEXT("A ragdoll that starts at rest gets no launch velocity"), RestingLaunch.LinearVelocity.IsNearlyZero() && RestingLaunch.AngularVelocityRadians.IsNearlyZero());
	TestFalse(TEXT("A ragdoll that starts at rest does not watch for a landing"), RestingLaunch.bWatchesLanding);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathLaunchCorpseSoundSettingsTest, "TDGame.Combat.DeathLaunch.Landing.CorpseSoundSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathLaunchCorpseSoundSettingsTest::RunTest(const FString& Parameters)
{
	const UTDCombatFeedbackSettings& FeedbackSettings = *GetDefault<UTDCombatFeedbackSettings>();
	const FSoftObjectPath KillThudPath(TEXT("/Game/Combat/Feedback/Audio/SW_TD_KillThud.SW_TD_KillThud"));
	const FSoftObjectPath BodyFallPath(TEXT("/Game/Combat/Feedback/Audio/SW_TD_BodyFall.SW_TD_BodyFall"));
	const auto IsBodyFallSound = [&BodyFallPath](const TSoftObjectPtr<USoundBase>& Sound)
	{
		return Sound.ToSoftObjectPath() == BodyFallPath;
	};

	TestEqual(TEXT("The kill sound holds a single sound"), FeedbackSettings.KillSound.Sounds.Num(), 1);
	TestTrue(TEXT("The kill sound is the kill thud"), FeedbackSettings.KillSound.Sounds.Num() == 1 && FeedbackSettings.KillSound.Sounds[0].ToSoftObjectPath() == KillThudPath);
	TestTrue(TEXT("The corpse land sound plays the body fall"), FeedbackSettings.CorpseLandSound.Sounds.ContainsByPredicate(IsBodyFallSound));

	TArray<FSoftObjectPath> PreloadPaths;
	FeedbackSettings.CollectSoftObjectPaths(PreloadPaths);
	TestTrue(TEXT("The feedback preload paths include the body fall"), PreloadPaths.Contains(BodyFallPath));
	return true;
}

#endif
