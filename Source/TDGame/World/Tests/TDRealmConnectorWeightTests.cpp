#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "World/Streaming/TDRealmSubsystem.h"

namespace
{
	constexpr float TDRealmConnectorWeightTestTolerance = 1e-5f;
	constexpr double TDRealmConnectorWeightTestStepCm = 1.0;
	constexpr double TDRealmConnectorWeightTestOutsideCm = 500.0;
	constexpr double TDRealmConnectorWeightTestSlopeMargin = 1.01;
	constexpr double TDRealmLiftShaftWeightTestTravelCm = 4000.0;
	constexpr double TDRealmLiftShaftWeightTestMidDepthCm = 2000.0;
	constexpr float TDRealmLiftShaftWeightTestMidTolerance = 0.02f;

	struct FTDRealmConnectorWeightTestCase
	{
		double ApproachLengthCm;
		double DepartureLengthCm;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmConnectorWeightTest, "TDGame.Realm.ConnectorWeight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmConnectorWeightTest::RunTest(const FString& Parameters)
{
	const FTDRealmConnectorWeightTestCase Cases[] =
	{
		{ 6400.0, 24167.0 },
		{ 19718.0, 9259.0 },
		{ 300.0, 300.0 }
	};

	for (const FTDRealmConnectorWeightTestCase& Case : Cases)
	{
		const double ApproachCm = Case.ApproachLengthCm;
		const double DepartureCm = Case.DepartureLengthCm;
		const FString Prefix = FString::Printf(TEXT("approach %.0f cm, departure %.0f cm"), ApproachCm, DepartureCm);
		TestNearlyEqual(FString::Printf(TEXT("%s: weight at -approach"), *Prefix), UTDRealmSubsystem::ComputeConnectorWeight(-ApproachCm, ApproachCm, DepartureCm), 0.0f, TDRealmConnectorWeightTestTolerance);
		TestNearlyEqual(FString::Printf(TEXT("%s: weight at the boundary"), *Prefix), UTDRealmSubsystem::ComputeConnectorWeight(0.0, ApproachCm, DepartureCm), 0.5f, TDRealmConnectorWeightTestTolerance);
		TestNearlyEqual(FString::Printf(TEXT("%s: weight at +departure"), *Prefix), UTDRealmSubsystem::ComputeConnectorWeight(DepartureCm, ApproachCm, DepartureCm), 1.0f, TDRealmConnectorWeightTestTolerance);
		TestNearlyEqual(FString::Printf(TEXT("%s: weight before the connector"), *Prefix), UTDRealmSubsystem::ComputeConnectorWeight(-ApproachCm - TDRealmConnectorWeightTestOutsideCm, ApproachCm, DepartureCm), 0.0f, TDRealmConnectorWeightTestTolerance);
		TestNearlyEqual(FString::Printf(TEXT("%s: weight after the connector"), *Prefix), UTDRealmSubsystem::ComputeConnectorWeight(DepartureCm + TDRealmConnectorWeightTestOutsideCm, ApproachCm, DepartureCm), 1.0f, TDRealmConnectorWeightTestTolerance);

		const double MaxStepChange = 0.5 * 1.5 * TDRealmConnectorWeightTestStepCm / FMath::Min(ApproachCm, DepartureCm) * TDRealmConnectorWeightTestSlopeMargin;
		double LargestStepChange = 0.0;
		int32 DecreasingStepCount = 0;
		float PreviousWeight = UTDRealmSubsystem::ComputeConnectorWeight(-ApproachCm - TDRealmConnectorWeightTestOutsideCm, ApproachCm, DepartureCm);
		for (double AlongCm = -ApproachCm - TDRealmConnectorWeightTestOutsideCm + TDRealmConnectorWeightTestStepCm; AlongCm <= DepartureCm + TDRealmConnectorWeightTestOutsideCm; AlongCm += TDRealmConnectorWeightTestStepCm)
		{
			const float Weight = UTDRealmSubsystem::ComputeConnectorWeight(AlongCm, ApproachCm, DepartureCm);
			DecreasingStepCount += Weight < PreviousWeight ? 1 : 0;
			LargestStepChange = FMath::Max(LargestStepChange, static_cast<double>(FMath::Abs(Weight - PreviousWeight)));
			PreviousWeight = Weight;
		}
		TestEqual(FString::Printf(TEXT("%s: steps where the weight decreases"), *Prefix), DecreasingStepCount, 0);
		TestTrue(FString::Printf(TEXT("%s: largest change per %.0f cm is %.6f (<= %.6f)"), *Prefix, TDRealmConnectorWeightTestStepCm, LargestStepChange, MaxStepChange), LargestStepChange <= MaxStepChange);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRealmLiftShaftWeightTest, "TDGame.Realm.LiftShaftWeight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRealmLiftShaftWeightTest::RunTest(const FString& Parameters)
{
	const double TravelCm = TDRealmLiftShaftWeightTestTravelCm;
	TestNearlyEqual(TEXT("weight at the top landing"), UTDRealmSubsystem::ComputeLiftShaftWeight(0.0, TravelCm), 0.0f, TDRealmConnectorWeightTestTolerance);
	TestNearlyEqual(TEXT("weight above the top landing"), UTDRealmSubsystem::ComputeLiftShaftWeight(-300.0, TravelCm), 0.0f, TDRealmConnectorWeightTestTolerance);
	TestNearlyEqual(TEXT("weight at 20 m depth"), UTDRealmSubsystem::ComputeLiftShaftWeight(TDRealmLiftShaftWeightTestMidDepthCm, TravelCm), 0.5f, TDRealmLiftShaftWeightTestMidTolerance);
	TestNearlyEqual(TEXT("weight at the bottom landing"), UTDRealmSubsystem::ComputeLiftShaftWeight(TravelCm, TravelCm), 1.0f, TDRealmConnectorWeightTestTolerance);
	TestNearlyEqual(TEXT("weight below the bottom landing"), UTDRealmSubsystem::ComputeLiftShaftWeight(TravelCm + 300.0, TravelCm), 1.0f, TDRealmConnectorWeightTestTolerance);

	int32 DecreasingStepCount = 0;
	float PreviousWeight = UTDRealmSubsystem::ComputeLiftShaftWeight(0.0, TravelCm);
	for (double DepthCm = TDRealmConnectorWeightTestStepCm; DepthCm <= TravelCm; DepthCm += TDRealmConnectorWeightTestStepCm)
	{
		const float Weight = UTDRealmSubsystem::ComputeLiftShaftWeight(DepthCm, TravelCm);
		DecreasingStepCount += Weight < PreviousWeight ? 1 : 0;
		PreviousWeight = Weight;
	}
	TestEqual(TEXT("steps where the shaft weight decreases"), DecreasingStepCount, 0);
	return true;
}

#endif
