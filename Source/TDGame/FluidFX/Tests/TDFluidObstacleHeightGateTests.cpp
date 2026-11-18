#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidObstacleComponent.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"

namespace TDFluidObstacleHeightGateTestPrivate
{
	FTDFluidSdfObstacle MakeBox(const FVector& HalfExtentsCm, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		return UTDFluidObstacleComponent::MakeOrientedBoxObstacle(FVector(100.0, 200.0, 300.0), HalfExtentsCm, Rotation);
	}

	FTDFluidSdfObstacle MakeSphere(float RadiusCm)
	{
		FTDFluidSdfObstacle Obstacle;
		Obstacle.WorldCenter = FVector(-50.0, 10.0, 5.0);
		Obstacle.RadiusCm = RadiusCm;
		Obstacle.bIsBox = false;
		return Obstacle;
	}

	FTDFluidSdfObstacle MakeBoxWithoutHeightInformation()
	{
		FTDFluidSdfObstacle Obstacle;
		Obstacle.WorldCenter = FVector(7.0, 8.0, 9.0);
		Obstacle.bIsBox = true;
		Obstacle.BoxHalfExtentsCm = FVector2D(500.0, 400.0);
		return Obstacle;
	}

	bool IsSameObstacle(const FTDFluidSdfObstacle& Left, const FTDFluidSdfObstacle& Right)
	{
		return Left.WorldCenter == Right.WorldCenter
			&& Left.RadiusCm == Right.RadiusCm
			&& Left.BoxHalfExtentsCm == Right.BoxHalfExtentsCm
			&& Left.bIsBox == Right.bIsBox
			&& Left.BoxHalfExtents3DCm == Right.BoxHalfExtents3DCm
			&& Left.WorldRotation == Right.WorldRotation;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidObstacleWorldHeightTest,
	"TDGame.FluidFX.ObstacleHeightGate.WorldHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidObstacleWorldHeightTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidObstacleHeightGateTestPrivate;
	constexpr float Tolerance = 0.01f;

	TestEqual(TEXT("Unrotated box height is twice the vertical half extent"), UTDFluidLiveComponent::ComputeObstacleWorldHeightCm(MakeBox(FVector(50.0, 60.0, 90.0))), 180.0f, Tolerance);
	TestEqual(TEXT("Yaw does not change the height"), UTDFluidLiveComponent::ComputeObstacleWorldHeightCm(MakeBox(FVector(50.0, 60.0, 15.0), FRotator(0.0, 37.0, 0.0))), 30.0f, Tolerance);
	TestEqual(TEXT("Pitch of 90 degrees stands the long axis up"), UTDFluidLiveComponent::ComputeObstacleWorldHeightCm(MakeBox(FVector(50.0, 60.0, 15.0), FRotator(90.0, 0.0, 0.0))), 100.0f, Tolerance);
	TestEqual(TEXT("Roll of 90 degrees stands the side axis up"), UTDFluidLiveComponent::ComputeObstacleWorldHeightCm(MakeBox(FVector(10.0, 40.0, 90.0), FRotator(0.0, 0.0, 90.0))), 80.0f, Tolerance);

	const float ExpectedTiltedHeightCm = static_cast<float>(2.0 * FMath::Sqrt(0.5) * (50.0 + 10.0));
	TestEqual(TEXT("Pitch of 45 degrees mixes two axes"), UTDFluidLiveComponent::ComputeObstacleWorldHeightCm(MakeBox(FVector(50.0, 50.0, 10.0), FRotator(45.0, 0.0, 0.0))), ExpectedTiltedHeightCm, Tolerance);

	TestEqual(TEXT("A sphere has no known height"), UTDFluidLiveComponent::ComputeObstacleWorldHeightCm(MakeSphere(40.0f)), 0.0f);
	TestEqual(TEXT("A box without three axis extents has no known height"), UTDFluidLiveComponent::ComputeObstacleWorldHeightCm(MakeBoxWithoutHeightInformation()), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidObstacleHeightGateOffKeepsListTest,
	"TDGame.FluidFX.ObstacleHeightGate.OffKeepsList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidObstacleHeightGateOffKeepsListTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidObstacleHeightGateTestPrivate;

	TestEqual(TEXT("The height gate is off by default"), FTDFluidMassInputSettings().ObstacleMinBlockHeightCm, 0.0f);

	TArray<FTDFluidSdfObstacle> Input;
	Input.Add(MakeBox(FVector(50.0, 60.0, 15.0), FRotator(0.0, 20.0, 0.0)));
	Input.Add(MakeSphere(40.0f));
	Input.Add(MakeBox(FVector(50.0, 60.0, 90.0)));
	Input.Add(MakeBoxWithoutHeightInformation());

	for (const float DisabledThresholdCm : { 0.0f, -10.0f })
	{
		TArray<FTDFluidSdfObstacle> Output;
		UTDFluidLiveComponent::AppendObstaclesTallEnoughToBlock(Input, DisabledThresholdCm, Output);
		if (!TestEqual(FString::Printf(TEXT("Threshold %.0f keeps every obstacle"), DisabledThresholdCm), Output.Num(), Input.Num()))
		{
			continue;
		}
		for (int32 Index = 0; Index < Input.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("Threshold %.0f keeps obstacle %d unchanged"), DisabledThresholdCm, Index), IsSameObstacle(Input[Index], Output[Index]));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidObstacleHeightGateDropsLowBoxesTest,
	"TDGame.FluidFX.ObstacleHeightGate.DropsLowBoxesOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidObstacleHeightGateDropsLowBoxesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidObstacleHeightGateTestPrivate;

	const FTDFluidSdfObstacle LowBox = MakeBox(FVector(300.0, 300.0, 15.0));
	const FTDFluidSdfObstacle TallBox = MakeBox(FVector(50.0, 60.0, 90.0));
	const FTDFluidSdfObstacle BoundaryBox = MakeBox(FVector(50.0, 60.0, 22.5));
	const FTDFluidSdfObstacle Sphere = MakeSphere(10.0f);
	const FTDFluidSdfObstacle UnknownHeightBox = MakeBoxWithoutHeightInformation();
	const FTDFluidSdfObstacle LyingTallBox = MakeBox(FVector(90.0, 60.0, 15.0), FRotator(90.0, 0.0, 0.0));

	TArray<FTDFluidSdfObstacle> Input;
	Input.Add(LowBox);
	Input.Add(TallBox);
	Input.Add(Sphere);
	Input.Add(UnknownHeightBox);
	Input.Add(BoundaryBox);
	Input.Add(LyingTallBox);

	TArray<FTDFluidSdfObstacle> Output;
	Output.Add(Sphere);
	UTDFluidLiveComponent::AppendObstaclesTallEnoughToBlock(Input, 45.0f, Output);

	if (!TestEqual(TEXT("Only the low box is dropped and existing output entries are kept"), Output.Num(), 6))
	{
		return false;
	}
	TestTrue(TEXT("Existing output entry stays first"), IsSameObstacle(Output[0], Sphere));
	TestTrue(TEXT("Tall box stays"), IsSameObstacle(Output[1], TallBox));
	TestTrue(TEXT("Sphere stays"), IsSameObstacle(Output[2], Sphere));
	TestTrue(TEXT("Box without height information stays"), IsSameObstacle(Output[3], UnknownHeightBox));
	TestTrue(TEXT("Box exactly at the threshold stays"), IsSameObstacle(Output[4], BoundaryBox));
	TestTrue(TEXT("Rotated box that is tall in the world stays"), IsSameObstacle(Output[5], LyingTallBox));
	return true;
}
