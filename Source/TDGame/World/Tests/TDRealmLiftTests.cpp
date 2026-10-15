#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "World/Streaming/TDRealmLiftDeck.h"
#include "World/Streaming/TDRealmShaftLiner.h"
#include "World/Streaming/TDRealmSubsystem.h"

namespace
{
	constexpr double TDRealmLiftTestStepSeconds = 1.0 / 60.0;
	constexpr double TDRealmLiftTestPositionToleranceCm = 1e-6;
	constexpr double TDRealmLiftTestSpeedToleranceCm = 1e-9;
	constexpr double TDRealmLiftTestInvariantToleranceCmPerSecond = 1.0;
	constexpr double TDRealmLiftTestLandingToleranceCm = 0.5;
	constexpr double TDRealmLiftTestPhaseToleranceCm = 0.5;
	constexpr double TDRealmLiftTestBoundaryWaitSeconds = 1.5;
	constexpr double TDRealmLiftTestRingToleranceCm = 0.1;
	constexpr double TDRealmLiftTestVisibleAboveDeckCm = 810.0;
	constexpr double TDRealmLiftTestMaxVisibleRingSlipCm = 2.0;
	constexpr double TDRealmLiftTestAbyssScreenDepthCm = 600.0;
	constexpr int32 TDRealmLiftTestStateCount = static_cast<int32>(ETDRealmLiftState::Calling) + 1;
	constexpr double TDRealmLiftTestNormalRideSeconds = 16.7;
	constexpr double TDRealmLiftTestNormalRideToleranceSeconds = 0.2;
	constexpr float TDRealmLiftTestPaceTolerance = 1e-5f;

	struct FTDRealmLiftTestTrace
	{
		int32 StepCount = 0;
		int32 TreadmillEntryCount = 0;
		int32 FirstTreadmillStep = INDEX_NONE;
		double FirstTreadmillProgressCm = -1.0;
		int32 HoldReachedStep = INDEX_NONE;
		double HoldProgressCm = -1.0;
		int32 ResumeArmedStep = INDEX_NONE;
		int32 ResumeStartStep = INDEX_NONE;
		int32 RingStopStep = INDEX_NONE;
		double RingStopPhaseCm = 0.0;
		double RingStopProgressCm = 0.0;
		int32 OpenStep = INDEX_NONE;
		double MaxSpeedSumErrorCmPerSecond = 0.0;
		double MaxDisplacementSumErrorCmPerSecond = 0.0;
		double MaxPositionCm = -TNumericLimits<double>::Max();
		double MinPositionCm = TNumericLimits<double>::Max();
		double MaxSpeedCmPerSecond = 0.0;
		double MaxSpeedIncreaseCmPerSecond = 0.0;
		double MaxStepMoveCm = 0.0;

		double GetBoundaryWaitSeconds() const
		{
			const int32 WaitStartStep = FMath::Max(ResumeArmedStep, HoldReachedStep);
			return (ResumeStartStep - WaitStartStep) * TDRealmLiftTestStepSeconds;
		}
	};

	void RecordTDRealmLiftTestTreadmillStep(const FTDRealmLiftMotion& Motion, ETDRealmLiftState PreviousState, double PreviousProgressCm, double PreviousPhaseCm, FTDRealmLiftTestTrace& Trace)
	{
		const bool bWasTreadmill = PreviousState == ETDRealmLiftState::Treadmill;
		const bool bIsTreadmill = Motion.State == ETDRealmLiftState::Treadmill;
		if (bIsTreadmill && !bWasTreadmill)
		{
			++Trace.TreadmillEntryCount;
			if (Trace.FirstTreadmillStep == INDEX_NONE)
			{
				Trace.FirstTreadmillStep = Trace.StepCount;
				Trace.FirstTreadmillProgressCm = Motion.GetProgressCm();
			}
		}
		if (!bIsTreadmill && !bWasTreadmill)
		{
			return;
		}

		const double SpeedSumCmPerSecond = Motion.SpeedCmPerSecond + Motion.RingSpeedCmPerSecond;
		const double DisplacementSumCmPerSecond = (Motion.GetProgressCm() - PreviousProgressCm + Motion.RingPhaseCm - PreviousPhaseCm) / TDRealmLiftTestStepSeconds;
		Trace.MaxSpeedSumErrorCmPerSecond = FMath::Max(Trace.MaxSpeedSumErrorCmPerSecond, FMath::Abs(SpeedSumCmPerSecond - Motion.CruiseSpeedCmPerSecond));
		Trace.MaxDisplacementSumErrorCmPerSecond = FMath::Max(Trace.MaxDisplacementSumErrorCmPerSecond, FMath::Abs(DisplacementSumCmPerSecond - Motion.CruiseSpeedCmPerSecond));
		if (bIsTreadmill && Motion.SpeedCmPerSecond <= 0.0 && Trace.HoldReachedStep == INDEX_NONE)
		{
			Trace.HoldReachedStep = Trace.StepCount;
			Trace.HoldProgressCm = Motion.GetProgressCm();
		}
		if (bIsTreadmill && Motion.bIsRingResuming && Trace.ResumeStartStep == INDEX_NONE)
		{
			Trace.ResumeStartStep = Trace.StepCount;
		}
		if (bWasTreadmill && Motion.State == ETDRealmLiftState::Riding && Trace.RingStopStep == INDEX_NONE)
		{
			Trace.RingStopStep = Trace.StepCount;
			Trace.RingStopPhaseCm = Motion.RingPhaseCm;
			Trace.RingStopProgressCm = Motion.GetProgressCm();
		}
	}

	void RunTDRealmLiftTestMotion(FTDRealmLiftMotion& Motion, double DurationSeconds, bool bHasRider, bool bIsReady, FTDRealmLiftTestTrace& Trace)
	{
		const int32 StepCount = FMath::RoundToInt(DurationSeconds / TDRealmLiftTestStepSeconds);
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			const ETDRealmLiftState PreviousState = Motion.State;
			const double PreviousProgressCm = Motion.GetProgressCm();
			const double PreviousPositionCm = Motion.PositionCm;
			const double PreviousPhaseCm = Motion.RingPhaseCm;
			const double PreviousSpeedCmPerSecond = Motion.SpeedCmPerSecond;
			const bool bWasResumeArmed = Motion.bIsResumeArmed;
			Motion.UpdateRider(bHasRider, TDRealmLiftTestStepSeconds);
			Motion.Step(TDRealmLiftTestStepSeconds, bIsReady);
			++Trace.StepCount;

			RecordTDRealmLiftTestTreadmillStep(Motion, PreviousState, PreviousProgressCm, PreviousPhaseCm, Trace);
			if (Motion.bIsResumeArmed && !bWasResumeArmed && Trace.ResumeArmedStep == INDEX_NONE)
			{
				Trace.ResumeArmedStep = Trace.StepCount;
			}
			if (Motion.State == ETDRealmLiftState::Open && Trace.OpenStep == INDEX_NONE)
			{
				Trace.OpenStep = Trace.StepCount;
			}
			Trace.MaxPositionCm = FMath::Max(Trace.MaxPositionCm, Motion.PositionCm);
			Trace.MinPositionCm = FMath::Min(Trace.MinPositionCm, Motion.PositionCm);
			Trace.MaxSpeedCmPerSecond = FMath::Max(Trace.MaxSpeedCmPerSecond, Motion.SpeedCmPerSecond);
			Trace.MaxSpeedIncreaseCmPerSecond = FMath::Max(Trace.MaxSpeedIncreaseCmPerSecond, Motion.SpeedCmPerSecond - PreviousSpeedCmPerSecond);
			Trace.MaxStepMoveCm = FMath::Max(Trace.MaxStepMoveCm, FMath::Abs(Motion.PositionCm - PreviousPositionCm));
		}
	}

	void BoardTDRealmLiftTestMotion(FTDRealmLiftMotion& Motion)
	{
		for (int32 StepIndex = 0; StepIndex < 1000 && !Motion.IsCarryingRider(); ++StepIndex)
		{
			Motion.UpdateRider(true, TDRealmLiftTestStepSeconds);
		}
	}

	double GetTDRealmLiftTestModuleError(double PhaseCm, double ModuleHeightCm)
	{
		const double Remainder = FMath::Fmod(PhaseCm, ModuleHeightCm);
		return FMath::Min(FMath::Abs(Remainder), FMath::Abs(ModuleHeightCm - Remainder));
	}

	TArray<double> ComputeTDRealmLiftTestRingTops(double PhaseCm, bool bIsDescending, double ModuleHeightCm = 450.0, int32 ModuleCount = 7, double RingTopDepthCm = 600.0)
	{
		TArray<double> RingTops;
		for (int32 RingIndex = 0; RingIndex < ModuleCount; ++RingIndex)
		{
			RingTops.Add(ATDRealmShaftLiner::ComputeRingTopDepthCm(RingIndex, PhaseCm, ModuleHeightCm, ModuleCount, RingTopDepthCm, bIsDescending));
		}
		return RingTops;
	}

	bool AreTDRealmLiftTestRingsContiguous(TArray<double> RingTops, double ModuleHeightCm, double& OutCoverTopCm, double& OutCoverBottomCm)
	{
		RingTops.Sort();
		OutCoverTopCm = RingTops[0];
		OutCoverBottomCm = RingTops.Last() + ModuleHeightCm;
		for (int32 RingIndex = 1; RingIndex < RingTops.Num(); ++RingIndex)
		{
			if (!FMath::IsNearlyEqual(RingTops[RingIndex] - RingTops[RingIndex - 1], ModuleHeightCm, TDRealmLiftTestRingToleranceCm))
			{
				return false;
			}
		}
		return true;
	}

	struct FTDRealmLiftTestWindowResult
	{
		int32 CheckedStepCount = 0;
		int32 BrokenRingStepCount = 0;
		double WorstVisibleMismatchCm = 0.0;
		bool bHasOpened = false;
	};

	double MeasureTDRealmLiftTestVisibleSpanCm(double WindowTopCm, double WindowBottomCm, double FirstEdgeCm, double SecondEdgeCm)
	{
		const double SpanTopCm = FMath::Max(WindowTopCm, FMath::Min(FirstEdgeCm, SecondEdgeCm));
		const double SpanBottomCm = FMath::Min(WindowBottomCm, FMath::Max(FirstEdgeCm, SecondEdgeCm));
		return FMath::Max(SpanBottomCm - SpanTopCm, 0.0);
	}

	FTDRealmLiftTestWindowResult RunTDRealmLiftTestTreadmillWindow(FTDRealmLiftMotion& Motion)
	{
		const double ModuleHeightCm = 450.0;
		const double RingTopDepthCm = 600.0;
		const double LinerBottomDepthCm = 3750.0;
		const double StartPhaseCm = Motion.RingPhaseCm;
		const bool bIsDescending = Motion.Direction == ETDRealmLiftDirection::Down;
		FTDRealmLiftTestWindowResult Result;
		for (int32 StepIndex = 0; StepIndex < 60 * 60 && Motion.State != ETDRealmLiftState::Open; ++StepIndex)
		{
			const bool bIsReady = StepIndex > 60 * 14;
			Motion.UpdateRider(true, TDRealmLiftTestStepSeconds);
			Motion.Step(TDRealmLiftTestStepSeconds, bIsReady);
			if (Motion.State != ETDRealmLiftState::Treadmill)
			{
				continue;
			}

			double CoverTopCm = 0.0;
			double CoverBottomCm = 0.0;
			const bool bIsContiguous = AreTDRealmLiftTestRingsContiguous(ComputeTDRealmLiftTestRingTops(Motion.RingPhaseCm - StartPhaseCm, bIsDescending), ModuleHeightCm, CoverTopCm, CoverBottomCm);
			const double WindowTopCm = Motion.PositionCm - TDRealmLiftTestVisibleAboveDeckCm;
			const double WindowBottomCm = Motion.PositionCm + TDRealmLiftTestAbyssScreenDepthCm;
			const double VisibleMismatchCm = MeasureTDRealmLiftTestVisibleSpanCm(WindowTopCm, WindowBottomCm, CoverTopCm, RingTopDepthCm)
				+ MeasureTDRealmLiftTestVisibleSpanCm(WindowTopCm, WindowBottomCm, CoverBottomCm, LinerBottomDepthCm);
			++Result.CheckedStepCount;
			Result.BrokenRingStepCount += bIsContiguous ? 0 : 1;
			Result.WorstVisibleMismatchCm = FMath::Max(Result.WorstVisibleMismatchCm, VisibleMismatchCm);
		}
		Result.bHasOpened = Motion.State == ETDRealmLiftState::Open;
		return Result;
	}

	void CheckTDRealmLiftTestTreadmillWindow(FAutomationTestBase& Test, FTDRealmLiftMotion& Motion, const TCHAR* CaseName)
	{
		const FTDRealmLiftTestWindowResult Result = RunTDRealmLiftTestTreadmillWindow(Motion);
		Test.TestTrue(FString::Printf(TEXT("%s: the treadmill ran (%d steps checked)"), CaseName, Result.CheckedStepCount), Result.CheckedStepCount > 60);
		Test.TestEqual(FString::Printf(TEXT("%s: steps with non-contiguous rings"), CaseName), Result.BrokenRingStepCount, 0);
		Test.TestTrue(FString::Printf(TEXT("%s: visible ring gap or ring-over-static slip %.3f cm stays within %.1f cm"), CaseName, Result.WorstVisibleMismatchCm, TDRealmLiftTestMaxVisibleRingSlipCm),
			Result.WorstVisibleMismatchCm <= TDRealmLiftTestMaxVisibleRingSlipCm);
		Test.TestTrue(FString::Printf(TEXT("%s: the ride finishes open"), CaseName), Result.bHasOpened);
	}

	struct FTDRealmLiftTestScreenTally
	{
		int32 StateStepCounts[TDRealmLiftTestStateCount] = {};
		int32 ShownStepCounts[TDRealmLiftTestStateCount] = {};
		double MinShownFloorClearanceCm = TNumericLimits<double>::Max();

		int32 GetStateSteps(ETDRealmLiftState State) const { return StateStepCounts[static_cast<int32>(State)]; }
		int32 GetShownSteps(ETDRealmLiftState State) const { return ShownStepCounts[static_cast<int32>(State)]; }
	};

	void RunTDRealmLiftTestScreen(FTDRealmLiftMotion& Motion, double DurationSeconds, bool bHasRider, bool bIsReady, FTDRealmLiftTestScreenTally& Tally)
	{
		const int32 StepCount = FMath::RoundToInt(DurationSeconds / TDRealmLiftTestStepSeconds);
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Motion.UpdateRider(bHasRider, TDRealmLiftTestStepSeconds);
			Motion.Step(TDRealmLiftTestStepSeconds, bIsReady);
			const int32 StateIndex = static_cast<int32>(Motion.State);
			const bool bIsShown = Motion.ShouldShowAbyssScreen(TDRealmLiftTestAbyssScreenDepthCm);
			++Tally.StateStepCounts[StateIndex];
			Tally.ShownStepCounts[StateIndex] += bIsShown ? 1 : 0;
			if (bIsShown)
			{
				Tally.MinShownFloorClearanceCm = FMath::Min(Tally.MinShownFloorClearanceCm, Motion.TravelLengthCm - Motion.PositionCm);
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftMakeFromGateTest, "TDGame.RealmLift.Motion.MakeFromGate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmLiftMakeFromGateTest::RunTest(const FString& Parameters)
{
	FTDRealmGate Gate;
	Gate.GateKind = ETDRealmGateKind::Lift;
	Gate.Lift.FieldLandingTransform = FTransform(FRotator(0.0, 135.0, 0.0), FVector(9200.0, -47800.0, 600.0));
	Gate.Lift.RealmLandingTransform = FTransform(FRotator(0.0, 225.0, 0.0), FVector(9200.0, -47800.0, -3400.0));
	const FTDRealmLiftMotion Motion = FTDRealmLiftMotion::MakeFromGate(Gate);
	TestEqual(TEXT("travel length is the landing height difference"), Motion.TravelLengthCm, 4000.0);
	TestEqual(TEXT("hold depth comes from the lift"), Motion.HoldDepthCm, static_cast<double>(Gate.Lift.HoldDepthCm));
	TestEqual(TEXT("ring module height comes from the lift"), Motion.RingModuleHeightCm, static_cast<double>(Gate.Lift.RingModuleHeightCm));
	TestEqual(TEXT("maximum treadmill time comes from the lift"), Motion.MaxTreadmillSeconds, static_cast<double>(Gate.Lift.MaxTreadmillSeconds));
	TestNearlyEqual(TEXT("decision point is the hold depth minus the brake distance"), Motion.GetTreadmillDecisionProgressCm(), 1350.0, TDRealmLiftTestPositionToleranceCm);
	TestTrue(TEXT("the default lift can use the treadmill"), Motion.CanUseTreadmill());

	FTDRealmLiftMotion DeepHoldMotion = Motion;
	DeepHoldMotion.HoldDepthCm = 3500.0;
	TestFalse(TEXT("a hold too deep to resume and brake disables the treadmill"), DeepHoldMotion.CanUseTreadmill());

	const FTransform ShaftFrame = ATDRealmLiftDeck::MakeShaftFrameTransform(Gate);
	TestNearlyEqual(TEXT("shaft frame yaw is the realm exit yaw plus 180"), FRotator::NormalizeAxis(ShaftFrame.Rotator().Yaw), 45.0, 1e-6);
	TestTrue(TEXT("shaft frame origin is the field landing"), ShaftFrame.GetLocation().Equals(Gate.Lift.FieldLandingTransform.GetLocation(), 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftStopsExactlyTest, "TDGame.RealmLift.Motion.StopsExactlyAtLanding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmLiftStopsExactlyTest::RunTest(const FString& Parameters)
{
	FTDRealmLiftMotion Motion;
	TestTrue(TEXT("starts resting at the field landing"), Motion.State == ETDRealmLiftState::Resting && Motion.IsAtFieldLanding());
	BoardTDRealmLiftTestMotion(Motion);
	FTDRealmLiftTestTrace Trace;
	RunTDRealmLiftTestMotion(Motion, 40.0, true, true, Trace);
	const double RideSeconds = Trace.OpenStep * TDRealmLiftTestStepSeconds;
	TestEqual(TEXT("a ready ride never enters the treadmill"), Trace.TreadmillEntryCount, 0);
	TestTrue(TEXT("opens at the realm landing"), Motion.State == ETDRealmLiftState::Open && Motion.IsAtRealmLanding());
	TestNearlyEqual(TEXT("stops at the 4000 cm landing"), Motion.PositionCm, 4000.0, TDRealmLiftTestLandingToleranceCm);
	TestTrue(TEXT("never passes the landing"), Trace.MaxPositionCm <= Motion.TravelLengthCm);
	TestTrue(FString::Printf(TEXT("normal ride takes %.3f s (16.7 +- 0.2)"), RideSeconds), Trace.OpenStep != INDEX_NONE && FMath::Abs(RideSeconds - TDRealmLiftTestNormalRideSeconds) <= TDRealmLiftTestNormalRideToleranceSeconds);
	TestTrue(TEXT("speed never exceeds the cruise speed"), Trace.MaxSpeedCmPerSecond <= Motion.CruiseSpeedCmPerSecond + TDRealmLiftTestSpeedToleranceCm);
	TestTrue(TEXT("speed rises no faster than the acceleration"), Trace.MaxSpeedIncreaseCmPerSecond <= Motion.AccelerationCmPerSecondSquared * TDRealmLiftTestStepSeconds + TDRealmLiftTestSpeedToleranceCm);
	TestTrue(TEXT("reaches the cruise speed on a 40 m ride"), Trace.MaxSpeedCmPerSecond >= Motion.CruiseSpeedCmPerSecond - TDRealmLiftTestSpeedToleranceCm);
	TestEqual(TEXT("ring phase stays at zero on a ready ride"), Motion.RingPhaseCm, 0.0);

	FTDRealmLiftMotion ClampedMotion;
	BoardTDRealmLiftTestMotion(ClampedMotion);
	FTDRealmLiftTestTrace CruiseTrace;
	RunTDRealmLiftTestMotion(ClampedMotion, 5.0, true, true, CruiseTrace);
	const double PositionBeforeLongStepCm = ClampedMotion.PositionCm;
	const double SpeedBeforeLongStepCm = ClampedMotion.SpeedCmPerSecond;
	ClampedMotion.Step(1.0, true);
	const double LongStepMoveCm = ClampedMotion.PositionCm - PositionBeforeLongStepCm;
	TestTrue(FString::Printf(TEXT("a 1 s step is clamped to 0.05 s (moved %.3f cm)"), LongStepMoveCm),
		LongStepMoveCm > 0.0 && LongStepMoveCm <= (SpeedBeforeLongStepCm + ClampedMotion.AccelerationCmPerSecondSquared * 0.05) * 0.05 + TDRealmLiftTestPositionToleranceCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftTreadmillInvariantTest, "TDGame.RealmLift.Motion.TreadmillInvariant", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmLiftTreadmillInvariantTest::RunTest(const FString& Parameters)
{
	FTDRealmLiftMotion Motion;
	BoardTDRealmLiftTestMotion(Motion);
	TestTrue(TEXT("rides down from the field landing"), Motion.State == ETDRealmLiftState::Riding && Motion.Direction == ETDRealmLiftDirection::Down);

	FTDRealmLiftTestTrace Trace;
	RunTDRealmLiftTestMotion(Motion, 14.0, true, false, Trace);
	const double DecisionProgressCm = Motion.GetTreadmillDecisionProgressCm();
	TestEqual(TEXT("enters the treadmill once"), Trace.TreadmillEntryCount, 1);
	TestTrue(FString::Printf(TEXT("treadmill starts at the brake decision point (%.3f cm, decision %.1f cm)"), Trace.FirstTreadmillProgressCm, DecisionProgressCm),
		Trace.FirstTreadmillProgressCm >= DecisionProgressCm && Trace.FirstTreadmillProgressCm <= DecisionProgressCm + Motion.CruiseSpeedCmPerSecond * TDRealmLiftTestStepSeconds + TDRealmLiftTestPositionToleranceCm);
	TestTrue(TEXT("still on the treadmill while not ready"), Motion.State == ETDRealmLiftState::Treadmill);
	TestNearlyEqual(TEXT("deck holds at 1912.5 cm"), Motion.PositionCm, 1912.5, TDRealmLiftTestLandingToleranceCm);
	TestNearlyEqual(TEXT("hold progress recorded when the deck stopped"), Trace.HoldProgressCm, 1912.5, TDRealmLiftTestLandingToleranceCm);
	TestTrue(TEXT("never passes the hold depth while not ready"), Trace.MaxPositionCm <= Motion.HoldDepthCm + TDRealmLiftTestPositionToleranceCm);
	TestEqual(TEXT("deck speed at the hold"), Motion.SpeedCmPerSecond, 0.0);
	TestNearlyEqual(TEXT("ring runs at the cruise speed during the hold"), Motion.RingSpeedCmPerSecond, Motion.CruiseSpeedCmPerSecond, TDRealmLiftTestSpeedToleranceCm);
	TestFalse(TEXT("resume is not armed before ready or timeout"), Motion.bIsResumeArmed);

	RunTDRealmLiftTestMotion(Motion, 30.0, true, true, Trace);
	TestEqual(TEXT("still exactly one treadmill entry"), Trace.TreadmillEntryCount, 1);
	TestTrue(TEXT("opens at the realm landing"), Motion.State == ETDRealmLiftState::Open && Motion.IsAtRealmLanding());
	TestNearlyEqual(TEXT("stops at the 4000 cm landing after the treadmill"), Motion.PositionCm, 4000.0, TDRealmLiftTestLandingToleranceCm);
	TestTrue(FString::Printf(TEXT("deck + ring speed stays 300 cm/s (worst error %.6f)"), Trace.MaxSpeedSumErrorCmPerSecond), Trace.MaxSpeedSumErrorCmPerSecond <= TDRealmLiftTestInvariantToleranceCmPerSecond);
	TestTrue(FString::Printf(TEXT("deck + ring displacement stays 300 cm/s (worst error %.6f)"), Trace.MaxDisplacementSumErrorCmPerSecond), Trace.MaxDisplacementSumErrorCmPerSecond <= TDRealmLiftTestInvariantToleranceCmPerSecond);
	TestEqual(TEXT("ring stopped after the ride"), Motion.RingSpeedCmPerSecond, 0.0);
	TestTrue(TEXT("speed never exceeds the cruise speed"), Trace.MaxSpeedCmPerSecond <= Motion.CruiseSpeedCmPerSecond + TDRealmLiftTestSpeedToleranceCm);
	TestTrue(TEXT("deck moves continuously"), Trace.MaxStepMoveCm <= Motion.CruiseSpeedCmPerSecond * TDRealmLiftTestStepSeconds + TDRealmLiftTestPositionToleranceCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftTreadmillBoundaryTest, "TDGame.RealmLift.Motion.TreadmillResumesOnModuleBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmLiftTreadmillBoundaryTest::RunTest(const FString& Parameters)
{
	const double NotReadySecondsCases[] = { 5.0, 8.0, 9.9, 11.37, 12.6, 13.85 };
	double MaxBoundaryWaitSeconds = 0.0;
	for (const double NotReadySeconds : NotReadySecondsCases)
	{
		FTDRealmLiftMotion Motion;
		BoardTDRealmLiftTestMotion(Motion);
		FTDRealmLiftTestTrace Trace;
		RunTDRealmLiftTestMotion(Motion, NotReadySeconds, true, false, Trace);
		RunTDRealmLiftTestMotion(Motion, 30.0, true, true, Trace);
		const FString Prefix = FString::Printf(TEXT("ready after %.2f s"), NotReadySeconds);
		TestTrue(Prefix + TEXT(": opens at the realm landing"), Motion.State == ETDRealmLiftState::Open && Motion.IsAtRealmLanding());
		TestNearlyEqual(Prefix + TEXT(": lands at 4000 cm"), Motion.PositionCm, 4000.0, TDRealmLiftTestLandingToleranceCm);
		if (NotReadySeconds < 5.5)
		{
			TestEqual(Prefix + TEXT(": ready before the decision point skips the treadmill"), Trace.TreadmillEntryCount, 0);
			continue;
		}

		const double BoundaryWaitSeconds = Trace.GetBoundaryWaitSeconds();
		const double ResumeEndProgressCm = Motion.HoldDepthCm + FMath::Square(Motion.CruiseSpeedCmPerSecond) / (2.0 * Motion.AccelerationCmPerSecondSquared);
		MaxBoundaryWaitSeconds = FMath::Max(MaxBoundaryWaitSeconds, BoundaryWaitSeconds);
		TestEqual(Prefix + TEXT(": treadmill entered once"), Trace.TreadmillEntryCount, 1);
		TestTrue(Prefix + TEXT(": ring stopped"), Trace.RingStopStep != INDEX_NONE && Trace.ResumeStartStep != INDEX_NONE && Trace.ResumeArmedStep != INDEX_NONE);
		TestTrue(FString::Printf(TEXT("%s: ring phase %.6f cm stops on a 450 cm module boundary"), *Prefix, Trace.RingStopPhaseCm),
			GetTDRealmLiftTestModuleError(Trace.RingStopPhaseCm, Motion.RingModuleHeightCm) <= TDRealmLiftTestPhaseToleranceCm);
		TestTrue(FString::Printf(TEXT("%s: boundary wait %.3f s is at most 1.5 s"), *Prefix, BoundaryWaitSeconds),
			BoundaryWaitSeconds >= 0.0 && BoundaryWaitSeconds <= TDRealmLiftTestBoundaryWaitSeconds + TDRealmLiftTestStepSeconds);
		TestTrue(FString::Printf(TEXT("%s: ring stops at the resume end %.2f cm (expected %.2f)"), *Prefix, Trace.RingStopProgressCm, ResumeEndProgressCm),
			FMath::Abs(Trace.RingStopProgressCm - ResumeEndProgressCm) <= Motion.CruiseSpeedCmPerSecond * TDRealmLiftTestStepSeconds + TDRealmLiftTestLandingToleranceCm);
		TestTrue(FString::Printf(TEXT("%s: deck + ring speed invariant (worst %.6f, displacement %.6f)"), *Prefix, Trace.MaxSpeedSumErrorCmPerSecond, Trace.MaxDisplacementSumErrorCmPerSecond),
			Trace.MaxSpeedSumErrorCmPerSecond <= TDRealmLiftTestInvariantToleranceCmPerSecond && Trace.MaxDisplacementSumErrorCmPerSecond <= TDRealmLiftTestInvariantToleranceCmPerSecond);
	}
	TestTrue(FString::Printf(TEXT("at least one case waited for a module boundary (max %.3f s)"), MaxBoundaryWaitSeconds), MaxBoundaryWaitSeconds > 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftTreadmillTimeoutTest, "TDGame.RealmLift.Motion.TreadmillTimeoutProceeds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmLiftTreadmillTimeoutTest::RunTest(const FString& Parameters)
{
	FTDRealmLiftMotion Motion;
	BoardTDRealmLiftTestMotion(Motion);
	FTDRealmLiftTestTrace Trace;
	RunTDRealmLiftTestMotion(Motion, 45.0, true, false, Trace);
	const double ArmedAfterSeconds = (Trace.ResumeArmedStep - Trace.FirstTreadmillStep) * TDRealmLiftTestStepSeconds;
	TestEqual(TEXT("treadmill entered once"), Trace.TreadmillEntryCount, 1);
	TestTrue(FString::Printf(TEXT("resume armed %.3f s after the treadmill started (20 s timeout)"), ArmedAfterSeconds),
		Trace.ResumeArmedStep != INDEX_NONE && FMath::Abs(ArmedAfterSeconds - Motion.MaxTreadmillSeconds) <= 2.0 * TDRealmLiftTestStepSeconds);
	TestFalse(TEXT("the destination never became ready"), Motion.bIsReadyLatched);
	TestTrue(TEXT("the deck still opens at the realm landing"), Motion.State == ETDRealmLiftState::Open && Motion.IsAtRealmLanding());
	TestNearlyEqual(TEXT("lands at 4000 cm without readiness"), Motion.PositionCm, 4000.0, TDRealmLiftTestLandingToleranceCm);
	TestTrue(FString::Printf(TEXT("boundary wait %.3f s after the timeout"), Trace.GetBoundaryWaitSeconds()), Trace.GetBoundaryWaitSeconds() <= TDRealmLiftTestBoundaryWaitSeconds + TDRealmLiftTestStepSeconds);
	TestTrue(FString::Printf(TEXT("no teleport: largest step move %.4f cm"), Trace.MaxStepMoveCm), Trace.MaxStepMoveCm <= Motion.CruiseSpeedCmPerSecond * TDRealmLiftTestStepSeconds + TDRealmLiftTestPositionToleranceCm);
	TestTrue(TEXT("ring phase ends on a module boundary"), GetTDRealmLiftTestModuleError(Motion.RingPhaseCm, Motion.RingModuleHeightCm) <= TDRealmLiftTestPhaseToleranceCm);
	TestTrue(FString::Printf(TEXT("deck + ring speed invariant (worst %.6f)"), Trace.MaxSpeedSumErrorCmPerSecond), Trace.MaxSpeedSumErrorCmPerSecond <= TDRealmLiftTestInvariantToleranceCmPerSecond);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftAscentSymmetryTest, "TDGame.RealmLift.Motion.AscentIsSymmetric", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmLiftAscentSymmetryTest::RunTest(const FString& Parameters)
{
	FTDRealmLiftMotion DescentMotion;
	BoardTDRealmLiftTestMotion(DescentMotion);
	FTDRealmLiftTestTrace DescentTrace;
	RunTDRealmLiftTestMotion(DescentMotion, 14.0, true, false, DescentTrace);

	FTDRealmLiftMotion AscentMotion;
	AscentMotion.PlaceAtLanding(true);
	TestTrue(TEXT("placed resting at the realm landing"), AscentMotion.State == ETDRealmLiftState::Resting && AscentMotion.IsAtRealmLanding());
	BoardTDRealmLiftTestMotion(AscentMotion);
	TestTrue(TEXT("rides up from the realm landing"), AscentMotion.Direction == ETDRealmLiftDirection::Up && AscentMotion.State == ETDRealmLiftState::Riding);

	FTDRealmLiftTestTrace AscentTrace;
	RunTDRealmLiftTestMotion(AscentMotion, 14.0, true, false, AscentTrace);
	const double AscentHoldPositionCm = AscentMotion.TravelLengthCm - AscentMotion.HoldDepthCm;
	TestTrue(TEXT("ascent is on the treadmill"), AscentMotion.State == ETDRealmLiftState::Treadmill);
	TestNearlyEqual(TEXT("ascent holds at the same progress (2087.5 cm depth)"), AscentMotion.PositionCm, AscentHoldPositionCm, TDRealmLiftTestLandingToleranceCm);
	TestTrue(TEXT("ascent never passes the hold depth before ready"), AscentTrace.MinPositionCm >= AscentHoldPositionCm - TDRealmLiftTestPositionToleranceCm);
	TestTrue(FString::Printf(TEXT("ascent (%d) and descent (%d) enter the treadmill within one step"), AscentTrace.FirstTreadmillStep, DescentTrace.FirstTreadmillStep),
		AscentTrace.FirstTreadmillStep != INDEX_NONE && FMath::Abs(AscentTrace.FirstTreadmillStep - DescentTrace.FirstTreadmillStep) <= 1);
	TestTrue(FString::Printf(TEXT("ascent (%d) and descent (%d) reach the hold within one step"), AscentTrace.HoldReachedStep, DescentTrace.HoldReachedStep),
		AscentTrace.HoldReachedStep != INDEX_NONE && FMath::Abs(AscentTrace.HoldReachedStep - DescentTrace.HoldReachedStep) <= 1);

	RunTDRealmLiftTestMotion(AscentMotion, 30.0, true, true, AscentTrace);
	TestTrue(TEXT("ascent opens at the field landing"), AscentMotion.State == ETDRealmLiftState::Open && AscentMotion.IsAtFieldLanding());
	TestNearlyEqual(TEXT("ascent stops at the field landing"), AscentMotion.PositionCm, 0.0, TDRealmLiftTestLandingToleranceCm);
	TestTrue(FString::Printf(TEXT("ascent deck + ring speed invariant (worst %.6f, displacement %.6f)"), AscentTrace.MaxSpeedSumErrorCmPerSecond, AscentTrace.MaxDisplacementSumErrorCmPerSecond),
		AscentTrace.MaxSpeedSumErrorCmPerSecond <= TDRealmLiftTestInvariantToleranceCmPerSecond && AscentTrace.MaxDisplacementSumErrorCmPerSecond <= TDRealmLiftTestInvariantToleranceCmPerSecond);
	TestTrue(TEXT("ascent ring phase ends on a module boundary"), GetTDRealmLiftTestModuleError(AscentMotion.RingPhaseCm, AscentMotion.RingModuleHeightCm) <= TDRealmLiftTestPhaseToleranceCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftCallTest, "TDGame.RealmLift.Motion.CallMovesWithoutReadiness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmLiftCallTest::RunTest(const FString& Parameters)
{
	FTDRealmLiftMotion Motion;
	Motion.PlaceAtLanding(true);
	Motion.RequestCall(true);
	TestTrue(TEXT("a call to the landing the deck rests at is ignored"), Motion.State == ETDRealmLiftState::Resting);

	Motion.RequestCall(false);
	TestTrue(TEXT("a call to the field landing starts an upward call"), Motion.State == ETDRealmLiftState::Calling && Motion.Direction == ETDRealmLiftDirection::Up);
	FTDRealmLiftTestTrace CallTrace;
	RunTDRealmLiftTestMotion(Motion, 40.0, false, false, CallTrace);
	TestTrue(TEXT("the empty deck rests at the field landing"), Motion.State == ETDRealmLiftState::Resting && Motion.IsAtFieldLanding());
	TestTrue(TEXT("the empty deck moves no faster than the call speed"), CallTrace.MaxSpeedCmPerSecond <= Motion.CallSpeedCmPerSecond + TDRealmLiftTestSpeedToleranceCm);
	TestEqual(TEXT("a call never uses the treadmill"), CallTrace.TreadmillEntryCount, 0);

	FTDRealmLiftMotion LostRiderMotion;
	BoardTDRealmLiftTestMotion(LostRiderMotion);
	FTDRealmLiftTestTrace RideTrace;
	RunTDRealmLiftTestMotion(LostRiderMotion, 2.0, true, false, RideTrace);
	FTDRealmLiftTestTrace LostTrace;
	RunTDRealmLiftTestMotion(LostRiderMotion, 0.1, false, false, LostTrace);
	TestTrue(TEXT("a deck that loses its rider continues as a call"), LostRiderMotion.State == ETDRealmLiftState::Calling);
	RunTDRealmLiftTestMotion(LostRiderMotion, 60.0, false, false, LostTrace);
	TestTrue(TEXT("the empty deck finishes at the realm landing"), LostRiderMotion.State == ETDRealmLiftState::Resting && LostRiderMotion.IsAtRealmLanding());

	FTDRealmLiftMotion TreadmillLostMotion;
	BoardTDRealmLiftTestMotion(TreadmillLostMotion);
	FTDRealmLiftTestTrace TreadmillRideTrace;
	RunTDRealmLiftTestMotion(TreadmillLostMotion, 11.0, true, false, TreadmillRideTrace);
	TestTrue(TEXT("the ride is on the treadmill before the rider is lost"), TreadmillLostMotion.State == ETDRealmLiftState::Treadmill && TreadmillLostMotion.RingPhaseCm > 0.0);
	FTDRealmLiftTestTrace TreadmillLostTrace;
	RunTDRealmLiftTestMotion(TreadmillLostMotion, 0.1, false, false, TreadmillLostTrace);
	TestTrue(TEXT("losing the rider on the treadmill turns into a call"), TreadmillLostMotion.State == ETDRealmLiftState::Calling);
	TestEqual(TEXT("the ring stops when the treadmill is abandoned"), TreadmillLostMotion.RingSpeedCmPerSecond, 0.0);
	TestTrue(FString::Printf(TEXT("abandoned ring phase %.6f cm snaps to a module boundary"), TreadmillLostMotion.RingPhaseCm),
		GetTDRealmLiftTestModuleError(TreadmillLostMotion.RingPhaseCm, TreadmillLostMotion.RingModuleHeightCm) <= TDRealmLiftTestPositionToleranceCm);
	RunTDRealmLiftTestMotion(TreadmillLostMotion, 60.0, false, false, TreadmillLostTrace);
	TestTrue(TEXT("the abandoned deck finishes at the realm landing"), TreadmillLostMotion.State == ETDRealmLiftState::Resting && TreadmillLostMotion.IsAtRealmLanding());
	TestEqual(TEXT("the empty deck never re-enters the treadmill"), TreadmillLostTrace.TreadmillEntryCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftReadyHysteresisTest, "TDGame.RealmLift.Motion.ReadyHysteresis", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmLiftReadyHysteresisTest::RunTest(const FString& Parameters)
{
	FTDRealmLiftMotion Motion;
	BoardTDRealmLiftTestMotion(Motion);
	FTDRealmLiftTestTrace Trace;
	RunTDRealmLiftTestMotion(Motion, 12.0, true, false, Trace);
	for (int32 BlipIndex = 0; BlipIndex < 10; ++BlipIndex)
	{
		RunTDRealmLiftTestMotion(Motion, 0.2, true, true, Trace);
		RunTDRealmLiftTestMotion(Motion, 0.2, true, false, Trace);
	}
	TestFalse(TEXT("0.2 s ready blips do not latch"), Motion.bIsReadyLatched);
	TestFalse(TEXT("0.2 s ready blips do not arm the resume"), Motion.bIsResumeArmed);
	TestNearlyEqual(TEXT("deck stays at the hold depth through ready blips"), Motion.PositionCm, Motion.HoldDepthCm, TDRealmLiftTestLandingToleranceCm);
	TestEqual(TEXT("deck speed stays zero through ready blips"), Motion.SpeedCmPerSecond, 0.0);

	RunTDRealmLiftTestMotion(Motion, 0.5, true, true, Trace);
	TestTrue(TEXT("0.5 s of ready latches"), Motion.bIsReadyLatched);
	TestTrue(TEXT("latching arms the resume"), Motion.bIsResumeArmed);
	RunTDRealmLiftTestMotion(Motion, TDRealmLiftTestBoundaryWaitSeconds + 0.1, true, true, Trace);
	TestTrue(TEXT("deck leaves the hold depth within the boundary wait"), Motion.PositionCm > Motion.HoldDepthCm && Motion.SpeedCmPerSecond > 0.0);

	RunTDRealmLiftTestMotion(Motion, 0.2, true, false, Trace);
	TestTrue(TEXT("a 0.2 s not-ready blip keeps the ready latch"), Motion.bIsReadyLatched);
	RunTDRealmLiftTestMotion(Motion, 0.4, true, false, Trace);
	TestFalse(TEXT("0.4 s of not ready clears the ready latch"), Motion.bIsReadyLatched);
	RunTDRealmLiftTestMotion(Motion, 30.0, true, false, Trace);
	TestTrue(TEXT("a resumed ride ignores later readiness and opens at the realm landing"), Motion.State == ETDRealmLiftState::Open && Motion.IsAtRealmLanding());
	TestEqual(TEXT("the resumed ride entered the treadmill only once"), Trace.TreadmillEntryCount, 1);

	FTDRealmLiftMotion PastDecisionMotion;
	BoardTDRealmLiftTestMotion(PastDecisionMotion);
	FTDRealmLiftTestTrace PastDecisionTrace;
	RunTDRealmLiftTestMotion(PastDecisionMotion, 7.0, true, true, PastDecisionTrace);
	TestTrue(TEXT("a ready ride passes the decision point"), PastDecisionMotion.GetProgressCm() > PastDecisionMotion.GetTreadmillDecisionProgressCm());
	RunTDRealmLiftTestMotion(PastDecisionMotion, 30.0, true, false, PastDecisionTrace);
	TestEqual(TEXT("losing readiness after the decision point does not start the treadmill"), PastDecisionTrace.TreadmillEntryCount, 0);
	TestTrue(TEXT("the ride past the decision point opens at the realm landing"), PastDecisionMotion.State == ETDRealmLiftState::Open && PastDecisionMotion.IsAtRealmLanding());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftRingPlacementTest, "TDGame.RealmLift.Liner.RingPlacement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmLiftRingPlacementTest::RunTest(const FString& Parameters)
{
	const double ModuleHeightCm = 450.0;
	const double RingTopDepthCm = 600.0;
	const double LinerBottomDepthCm = 3750.0;
	const double TreadmillWindowBottomCm = LinerBottomDepthCm - ModuleHeightCm;
	for (const bool bIsDescending : { true, false })
	{
		const TCHAR* DirectionName = bIsDescending ? TEXT("descending") : TEXT("ascending");
		for (const int32 ModuleSteps : { 0, 1, 2, 5, 7, 9, 23 })
		{
			TArray<double> RingTops = ComputeTDRealmLiftTestRingTops(ModuleSteps * ModuleHeightCm, bIsDescending);
			RingTops.Sort();
			bool bIsCanonical = true;
			for (int32 RingIndex = 0; RingIndex < RingTops.Num(); ++RingIndex)
			{
				bIsCanonical &= FMath::IsNearlyEqual(RingTops[RingIndex], RingTopDepthCm + RingIndex * ModuleHeightCm, TDRealmLiftTestRingToleranceCm);
			}
			TestTrue(FString::Printf(TEXT("%s, %d modules: 7 rings tile [600, 3750] without gap or overlap"), DirectionName, ModuleSteps), bIsCanonical);
		}

		for (const double PhaseCm : { 0.25, 37.5, 224.9, 449.9, 450.1, 1000.0, 3149.75, 5321.6 })
		{
			double CoverTopCm = 0.0;
			double CoverBottomCm = 0.0;
			const bool bIsContiguous = AreTDRealmLiftTestRingsContiguous(ComputeTDRealmLiftTestRingTops(PhaseCm, bIsDescending), ModuleHeightCm, CoverTopCm, CoverBottomCm);
			TestTrue(FString::Printf(TEXT("%s, phase %.2f: rings stay contiguous"), DirectionName, PhaseCm), bIsContiguous);
			TestTrue(FString::Printf(TEXT("%s, phase %.2f: cover [%.2f, %.2f] holds the treadmill window [600, 3300]"), DirectionName, PhaseCm, CoverTopCm, CoverBottomCm),
				CoverTopCm <= RingTopDepthCm + TDRealmLiftTestRingToleranceCm && CoverBottomCm >= TreadmillWindowBottomCm - TDRealmLiftTestRingToleranceCm);
			TestTrue(FString::Printf(TEXT("%s, phase %.2f: cover stays inside [150, 3750]"), DirectionName, PhaseCm),
				CoverTopCm >= RingTopDepthCm - ModuleHeightCm - TDRealmLiftTestRingToleranceCm && CoverBottomCm <= LinerBottomDepthCm + TDRealmLiftTestRingToleranceCm);
		}

		const double SmallPhaseCm = 10.0;
		const double MovedTopCm = ATDRealmShaftLiner::ComputeRingTopDepthCm(1, SmallPhaseCm, ModuleHeightCm, 7, RingTopDepthCm, bIsDescending);
		const double ExpectedTopCm = RingTopDepthCm + ModuleHeightCm + (bIsDescending ? -SmallPhaseCm : SmallPhaseCm);
		TestNearlyEqual(FString::Printf(TEXT("%s: a ring moves %s against the travel"), DirectionName, bIsDescending ? TEXT("up") : TEXT("down")), MovedTopCm, ExpectedTopCm, TDRealmLiftTestRingToleranceCm);

		int32 WrapCount = 0;
		int32 VisibleWrapCount = 0;
		for (int32 PhaseStep = 0; PhaseStep < 3150; ++PhaseStep)
		{
			for (int32 RingIndex = 0; RingIndex < 7; ++RingIndex)
			{
				const double BeforeTopCm = ATDRealmShaftLiner::ComputeRingTopDepthCm(RingIndex, PhaseStep, ModuleHeightCm, 7, RingTopDepthCm, bIsDescending);
				const double AfterTopCm = ATDRealmShaftLiner::ComputeRingTopDepthCm(RingIndex, PhaseStep + 1.0, ModuleHeightCm, 7, RingTopDepthCm, bIsDescending);
				if (FMath::Abs(AfterTopCm - BeforeTopCm) <= 2.0)
				{
					continue;
				}
				++WrapCount;
				auto IsOutsideWindow = [&](double TopCm)
				{
					return TopCm + ModuleHeightCm <= RingTopDepthCm + 2.0 || TopCm >= TreadmillWindowBottomCm - 2.0;
				};
				VisibleWrapCount += IsOutsideWindow(BeforeTopCm) && IsOutsideWindow(AfterTopCm) ? 0 : 1;
			}
		}
		TestEqual(FString::Printf(TEXT("%s: one wrap per module over a full loop"), DirectionName), WrapCount, 7);
		TestEqual(FString::Printf(TEXT("%s: wraps happen outside the treadmill window"), DirectionName), VisibleWrapCount, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftLinerWindowTest, "TDGame.RealmLift.Liner.TreadmillWindowCovered", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmLiftLinerWindowTest::RunTest(const FString& Parameters)
{
	FTDRealmLiftMotion DescentMotion;
	BoardTDRealmLiftTestMotion(DescentMotion);
	CheckTDRealmLiftTestTreadmillWindow(*this, DescentMotion, TEXT("descent"));

	FTDRealmLiftMotion AscentMotion;
	AscentMotion.PlaceAtLanding(true);
	BoardTDRealmLiftTestMotion(AscentMotion);
	CheckTDRealmLiftTestTreadmillWindow(*this, AscentMotion, TEXT("ascent"));

	FTDRealmLiftMotion ShallowHoldMotion;
	ShallowHoldMotion.HoldDepthCm = 1850.0;
	BoardTDRealmLiftTestMotion(ShallowHoldMotion);
	const FTDRealmLiftTestWindowResult ShallowHoldResult = RunTDRealmLiftTestTreadmillWindow(ShallowHoldMotion);
	TestTrue(FString::Printf(TEXT("a 1850 cm hold shows %.3f cm of ring slip over the static top, beyond %.1f cm"), ShallowHoldResult.WorstVisibleMismatchCm, TDRealmLiftTestMaxVisibleRingSlipCm),
		ShallowHoldResult.CheckedStepCount > 60 && ShallowHoldResult.WorstVisibleMismatchCm > TDRealmLiftTestMaxVisibleRingSlipCm);

	FTDRealmLiftMotion LongTravelMotion;
	LongTravelMotion.TravelLengthCm = 4100.0;
	LongTravelMotion.PlaceAtLanding(true);
	BoardTDRealmLiftTestMotion(LongTravelMotion);
	const FTDRealmLiftTestWindowResult LongTravelResult = RunTDRealmLiftTestTreadmillWindow(LongTravelMotion);
	TestTrue(FString::Printf(TEXT("a 4100 cm ascent shows %.3f cm of the ring gap below 3300 cm, beyond %.1f cm"), LongTravelResult.WorstVisibleMismatchCm, TDRealmLiftTestMaxVisibleRingSlipCm),
		LongTravelResult.CheckedStepCount > 60 && LongTravelResult.WorstVisibleMismatchCm > TDRealmLiftTestMaxVisibleRingSlipCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftAbyssScreenTest, "TDGame.RealmLift.Motion.AbyssScreenOnlyWithRider", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmLiftAbyssScreenTest::RunTest(const FString& Parameters)
{
	FTDRealmLiftMotion Motion;
	FTDRealmLiftTestScreenTally Tally;
	Motion.RequestCall(true);
	RunTDRealmLiftTestScreen(Motion, 40.0, false, false, Tally);
	TestTrue(TEXT("an empty call from the field landing reaches the realm landing"), Motion.State == ETDRealmLiftState::Resting && Motion.IsAtRealmLanding());
	const int32 DownCallSteps = Tally.GetStateSteps(ETDRealmLiftState::Calling);
	TestTrue(FString::Printf(TEXT("the downward call ran (%d steps)"), DownCallSteps), DownCallSteps > 60);
	TestEqual(TEXT("an empty downward call never shows the abyss screen"), Tally.GetShownSteps(ETDRealmLiftState::Calling), 0);

	Motion.RequestCall(false);
	RunTDRealmLiftTestScreen(Motion, 40.0, false, false, Tally);
	TestTrue(TEXT("an empty call from the realm landing reaches the field landing"), Motion.State == ETDRealmLiftState::Resting && Motion.IsAtFieldLanding());
	TestTrue(TEXT("the upward call ran"), Tally.GetStateSteps(ETDRealmLiftState::Calling) > DownCallSteps + 60);

	RunTDRealmLiftTestScreen(Motion, 12.0, true, false, Tally);
	TestTrue(TEXT("the descent with a rider is on the treadmill"), Motion.State == ETDRealmLiftState::Treadmill);
	RunTDRealmLiftTestScreen(Motion, 30.0, true, true, Tally);
	TestTrue(TEXT("the descent opens at the realm landing"), Motion.State == ETDRealmLiftState::Open && Motion.IsAtRealmLanding());
	RunTDRealmLiftTestScreen(Motion, 1.0, false, false, Tally);
	RunTDRealmLiftTestScreen(Motion, 30.0, true, true, Tally);
	TestTrue(TEXT("the ascent opens at the field landing"), Motion.State == ETDRealmLiftState::Open && Motion.IsAtFieldLanding());

	RunTDRealmLiftTestScreen(Motion, 1.0, false, false, Tally);
	RunTDRealmLiftTestScreen(Motion, 4.0, true, false, Tally);
	TestTrue(TEXT("a second descent is riding"), Motion.State == ETDRealmLiftState::Riding);
	const int32 CallingStepsBeforeLoss = Tally.GetStateSteps(ETDRealmLiftState::Calling);
	RunTDRealmLiftTestScreen(Motion, 40.0, false, false, Tally);
	TestTrue(TEXT("the deck that lost its rider continued as a call"), Tally.GetStateSteps(ETDRealmLiftState::Calling) > CallingStepsBeforeLoss);
	TestTrue(TEXT("the abandoned deck rests at the realm landing"), Motion.State == ETDRealmLiftState::Resting && Motion.IsAtRealmLanding());

	for (const ETDRealmLiftState HiddenState : { ETDRealmLiftState::Resting, ETDRealmLiftState::Boarding, ETDRealmLiftState::Open, ETDRealmLiftState::Calling })
	{
		TestTrue(FString::Printf(TEXT("state %d was visited"), static_cast<int32>(HiddenState)), Tally.GetStateSteps(HiddenState) > 0);
		TestEqual(FString::Printf(TEXT("state %d never shows the abyss screen"), static_cast<int32>(HiddenState)), Tally.GetShownSteps(HiddenState), 0);
	}
	TestTrue(TEXT("riding away from the floor shows the abyss screen"), Tally.GetShownSteps(ETDRealmLiftState::Riding) > 0);
	TestTrue(TEXT("riding near the floor hides the abyss screen"), Tally.GetShownSteps(ETDRealmLiftState::Riding) < Tally.GetStateSteps(ETDRealmLiftState::Riding));
	TestEqual(TEXT("every treadmill step shows the abyss screen"), Tally.GetShownSteps(ETDRealmLiftState::Treadmill), Tally.GetStateSteps(ETDRealmLiftState::Treadmill));
	TestTrue(FString::Printf(TEXT("the screen is shown only more than 600 cm above the floor (closest %.2f cm)"), Tally.MinShownFloorClearanceCm), Tally.MinShownFloorClearanceCm > TDRealmLiftTestAbyssScreenDepthCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmPassagePaceTargetTest, "TDGame.RealmLift.PassagePace.TargetTable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmPassagePaceTargetTest::RunTest(const FString& Parameters)
{
	struct FTDRealmPassagePaceCase
	{
		double ApproachCm;
		bool bIsReady;
		bool bIsPreloadRejected;
		bool bIsMovingAway;
		float ExpectedScale;
	};

	const FTDRealmPassagePaceCase Cases[] =
	{
		{ -5000.0, false, false, false, 1.0f },
		{ -2500.0, false, false, false, 1.0f },
		{ -1400.0, false, false, false, 0.65f },
		{ -300.0, false, false, false, 0.3f },
		{ 0.0, false, false, false, 0.3f },
		{ 2500.0, false, false, false, 0.3f },
		{ -300.0, true, false, false, 1.0f },
		{ -300.0, false, true, false, 1.0f },
		{ -300.0, false, false, true, 1.0f }
	};

	for (const FTDRealmPassagePaceCase& Case : Cases)
	{
		const float Scale = UTDRealmSubsystem::ComputePassagePaceTarget(Case.ApproachCm, Case.bIsReady, Case.bIsPreloadRejected, Case.bIsMovingAway);
		TestNearlyEqual(FString::Printf(TEXT("pace target at %.0f cm (ready %d, rejected %d, moving away %d)"), Case.ApproachCm, Case.bIsReady ? 1 : 0, Case.bIsPreloadRejected ? 1 : 0, Case.bIsMovingAway ? 1 : 0),
			Scale, Case.ExpectedScale, TDRealmLiftTestPaceTolerance);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmPassagePaceRateTest, "TDGame.RealmLift.PassagePace.RateLimit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmPassagePaceRateTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("slowing is limited to 0.5 per second"), UTDRealmSubsystem::StepPassagePaceScale(1.0f, 0.3f, 0.1f), 0.95f, TDRealmLiftTestPaceTolerance);
	TestNearlyEqual(TEXT("recovering is limited to 1.0 per second"), UTDRealmSubsystem::StepPassagePaceScale(0.3f, 1.0f, 0.1f), 0.4f, TDRealmLiftTestPaceTolerance);
	TestNearlyEqual(TEXT("slowing does not pass the target"), UTDRealmSubsystem::StepPassagePaceScale(0.32f, 0.3f, 0.1f), 0.3f, TDRealmLiftTestPaceTolerance);
	TestNearlyEqual(TEXT("recovering does not pass the target"), UTDRealmSubsystem::StepPassagePaceScale(0.95f, 1.0f, 0.1f), 1.0f, TDRealmLiftTestPaceTolerance);
	TestNearlyEqual(TEXT("a negative step keeps the scale"), UTDRealmSubsystem::StepPassagePaceScale(0.6f, 1.0f, -1.0f), 0.6f, TDRealmLiftTestPaceTolerance);
	return true;
}

#endif
