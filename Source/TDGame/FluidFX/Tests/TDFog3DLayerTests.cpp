#include "FluidFX/TDFog3DLayer.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "FluidFX/Tests/TDFog3DLayerTestAccess.h"
#include "FluidFX/TDFluidObstacleComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFog3DLayerTestPrivate
{
	struct FFollowReferenceCase
	{
		int32 WindowCells;
		FIntPoint Origin;
		FIntPoint PlayerCell;
		FIntPoint Expected;
	};

	FString DescribeCell(const FIntPoint& Cell)
	{
		return FString::Printf(TEXT("(%d, %d)"), Cell.X, Cell.Y);
	}

	FTDFog3DInputSample MakeSample(int32 SlotId, const FVector& World, float RadiusCm = 20.0f, float WindStrength = 1.0f, float ErosionPerSec = 0.0f)
	{
		FTDFog3DInputSample Sample;
		Sample.SlotId = SlotId;
		Sample.CurrentWorld = World;
		Sample.RadiusCm = RadiusCm;
		Sample.WindStrength = WindStrength;
		Sample.ErosionPerSec = ErosionPerSec;
		return Sample;
	}

	FTDFluidSdfObstacle MakeUprightBox(const FVector& Center, const FVector& HalfExtentsCm, double YawDegrees)
	{
		return UTDFluidObstacleComponent::MakeOrientedBoxObstacle(Center, HalfExtentsCm, FRotator(0.0, YawDegrees, 0.0));
	}

	FTDFog3DClockDecision MakeStepClock(float StepDeltaSeconds, float ElapsedSeconds)
	{
		FTDFog3DClockDecision Clock;
		Clock.bStepsNow = true;
		Clock.StepDeltaSeconds = StepDeltaSeconds;
		Clock.ElapsedSeconds = ElapsedSeconds;
		return Clock;
	}

	struct FClockRun
	{
		int32 StepCount = 0;
		int32 MaximumStepsInAnySecond = 0;
		float MaximumStepDeltaSeconds = 0.0f;
		float MinimumElapsedSeconds = std::numeric_limits<float>::max();
		float MaximumElapsedSeconds = 0.0f;
		float FirstStepDeltaSeconds = 0.0f;
		float FirstElapsedSeconds = 0.0f;
	};

	float MakeJitteredFrameSeconds(float FrameDeltaSeconds, float JitterSeconds, uint32& InOutLcgState)
	{
		if (JitterSeconds <= 0.0f)
		{
			return FrameDeltaSeconds;
		}
		InOutLcgState = InOutLcgState * 1664525u + 1013904223u;
		const double UnitValue = static_cast<double>(InOutLcgState) / 4294967296.0;
		return FrameDeltaSeconds + static_cast<float>((UnitValue * 2.0 - 1.0) * static_cast<double>(JitterSeconds));
	}

	int32 CountMostStepsInAnySecond(const TArray<double>& StepTimes)
	{
		int32 MostSteps = 0;
		int32 WindowStart = 0;
		for (int32 StepIndex = 0; StepIndex < StepTimes.Num(); ++StepIndex)
		{
			while (StepTimes[WindowStart] <= StepTimes[StepIndex] - 1.0)
			{
				++WindowStart;
			}
			MostSteps = FMath::Max(MostSteps, StepIndex - WindowStart + 1);
		}
		return MostSteps;
	}

	int32 CountStepsOverFrames(FTDFog3DLayer& Layer, float FrameSeconds, int32 FrameCount)
	{
		int32 StepCount = 0;
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			if (!Layer.AdvanceClock(FrameSeconds).bStepsNow)
			{
				continue;
			}
			FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, FTDFog3DStepRequest());
			++StepCount;
		}
		return StepCount;
	}

	FClockRun RunClock(FTDFog3DLayer& Layer, int32 FramesPerSecond, int32 Seconds, float JitterSeconds = 0.0f)
	{
		FClockRun Run;
		const float FrameDeltaSeconds = 1.0f / static_cast<float>(FramesPerSecond);
		uint32 LcgState = 12345u;
		double NowSeconds = 0.0;
		TArray<double> StepTimes;
		for (int32 FrameIndex = 0; FrameIndex < FramesPerSecond * Seconds; ++FrameIndex)
		{
			const float FrameSeconds = MakeJitteredFrameSeconds(FrameDeltaSeconds, JitterSeconds, LcgState);
			NowSeconds += static_cast<double>(FrameSeconds);
			const FTDFog3DClockDecision Decision = Layer.AdvanceClock(FrameSeconds);
			if (!Decision.bStepsNow)
			{
				continue;
			}
			FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, FTDFog3DStepRequest());
			StepTimes.Add(NowSeconds);
			if (Run.StepCount == 0)
			{
				Run.FirstStepDeltaSeconds = Decision.StepDeltaSeconds;
				Run.FirstElapsedSeconds = Decision.ElapsedSeconds;
			}
			++Run.StepCount;
			Run.MaximumStepDeltaSeconds = FMath::Max(Run.MaximumStepDeltaSeconds, Decision.StepDeltaSeconds);
			Run.MinimumElapsedSeconds = FMath::Min(Run.MinimumElapsedSeconds, Decision.ElapsedSeconds);
			Run.MaximumElapsedSeconds = FMath::Max(Run.MaximumElapsedSeconds, Decision.ElapsedSeconds);
		}
		Run.MaximumStepsInAnySecond = CountMostStepsInAnySecond(StepTimes);
		return Run;
	}

	bool AreSpheresEqual(const FTDFluidGpuFog3DSphere& Left, const FTDFluidGpuFog3DSphere& Right)
	{
		return Left.StartCmRadiusCm == Right.StartCmRadiusCm && Left.EndCmWindStrength == Right.EndCmWindStrength && Left.VelocityCmPerSecErosionPerSec == Right.VelocityCmPerSecErosionPerSec;
	}

	bool AreObstacleListsEqual(const TArray<FTDFluidGpuFog3DObstacle>& Left, const TArray<FTDFluidGpuFog3DObstacle>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 ObstacleIndex = 0; ObstacleIndex < Left.Num(); ++ObstacleIndex)
		{
			if (Left[ObstacleIndex].CenterCmYawCosine != Right[ObstacleIndex].CenterCmYawCosine || Left[ObstacleIndex].HalfExtentCmYawSine != Right[ObstacleIndex].HalfExtentCmYawSine)
			{
				return false;
			}
		}
		return true;
	}

	bool IsSphereAtRest(const FTDFluidGpuFog3DSphere& Sphere)
	{
		return Sphere.StartCmRadiusCm.X == Sphere.EndCmWindStrength.X
			&& Sphere.StartCmRadiusCm.Y == Sphere.EndCmWindStrength.Y
			&& Sphere.StartCmRadiusCm.Z == Sphere.EndCmWindStrength.Z
			&& Sphere.VelocityCmPerSecErosionPerSec.X == 0.0f
			&& Sphere.VelocityCmPerSecErosionPerSec.Y == 0.0f
			&& Sphere.VelocityCmPerSecErosionPerSec.Z == 0.0f;
	}

	FVector MakeCellCenterWorld(int32 CellX, int32 CellY)
	{
		const FTDFog3DSettings Settings;
		return FVector((static_cast<double>(CellX) + 0.5) * Settings.CellSizeCm, (static_cast<double>(CellY) + 0.5) * Settings.CellSizeCm, 0.0);
	}

	FTDFog3DStepRequest MakePathRequest(const FVector& PlayerWorld, const FVector* SampleWorld)
	{
		FTDFog3DStepRequest Request;
		Request.PlayerWorld = PlayerWorld;
		Request.FloorZ = 0.0;
		Request.Clock = MakeStepClock(1.0f / 60.0f, 1.0f / 60.0f);
		if (SampleWorld)
		{
			Request.Inputs.Add(MakeSample(0, *SampleWorld));
		}
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLayerWindowSnapAndFollowTest,
	"TDGame.FluidFX.Fog3D.WindowSnapAndFollow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLayerWindowSnapAndFollowTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DLayerTestPrivate;

	const double CellSizeCm = 15.625;
	TestTrue(TEXT("World cell at the origin"), FTDFog3DLayer::ComputeWorldCell(FVector2D(0.0, 0.0), CellSizeCm) == FIntPoint(0, 0));
	TestTrue(TEXT("World cell just below the first boundary"), FTDFog3DLayer::ComputeWorldCell(FVector2D(15.624, 31.25), CellSizeCm) == FIntPoint(0, 2));
	TestTrue(TEXT("World cell rounds down for negative coordinates"), FTDFog3DLayer::ComputeWorldCell(FVector2D(-0.001, -15.625), CellSizeCm) == FIntPoint(-1, -1));
	TestTrue(TEXT("World cell at a large coordinate"), FTDFog3DLayer::ComputeWorldCell(FVector2D(1.0e7, -1.0e7), CellSizeCm) == FIntPoint(640000, -640000));

	for (const int32 WindowCells : { 128, 256 })
	{
		for (int32 PlayerCellX = -1000; PlayerCellX <= 1000; ++PlayerCellX)
		{
			const FIntPoint Snapped = FTDFog3DLayer::SnapWindowMinCell(FIntPoint(PlayerCellX, -PlayerCellX), WindowCells);
			const bool bAligned = Snapped.X % 16 == 0 && Snapped.Y % 16 == 0;
			const int32 OffsetFromCenterX = PlayerCellX - (Snapped.X + WindowCells / 2);
			const int32 OffsetFromCenterY = -PlayerCellX - (Snapped.Y + WindowCells / 2);
			if (!bAligned || OffsetFromCenterX < -8 || OffsetFromCenterX >= 8 || OffsetFromCenterY < -8 || OffsetFromCenterY >= 8)
			{
				AddError(FString::Printf(TEXT("Snap of player cell %d with N %d gave %s"), PlayerCellX, WindowCells, *DescribeCell(Snapped)));
				break;
			}
		}
	}

	TestTrue(TEXT("Snap centers the first window on the player at cell 128"), FTDFog3DLayer::SnapWindowMinCell(FIntPoint(128, 128), 256) == FIntPoint(0, 0));
	TestTrue(TEXT("Snap rounds half a step up at cell 120"), FTDFog3DLayer::SnapWindowMinCell(FIntPoint(120, 120), 256) == FIntPoint(0, 0));
	TestTrue(TEXT("Snap rounds half a step down at cell 119"), FTDFog3DLayer::SnapWindowMinCell(FIntPoint(119, 119), 256) == FIntPoint(-16, -16));
	TestTrue(TEXT("Snap at cell zero uses the rounded half step"), FTDFog3DLayer::SnapWindowMinCell(FIntPoint(0, 0), 256) == FIntPoint(-128, -128));
	TestTrue(TEXT("Snap with a 128 cell window at cell zero"), FTDFog3DLayer::SnapWindowMinCell(FIntPoint(0, 0), 128) == FIntPoint(-64, -64));

	const FIntPoint Origin256(0, 0);
	TestTrue(TEXT("Twelve cells away keeps the window"), FTDFog3DLayer::FollowWindowMinCell(Origin256, FIntPoint(140, 128), 256, 12) == Origin256);
	TestTrue(TEXT("Twelve cells away on the negative side keeps the window"), FTDFog3DLayer::FollowWindowMinCell(Origin256, FIntPoint(116, 128), 256, 12) == Origin256);
	TestTrue(TEXT("Thirteen cells away on x moves only x"), FTDFog3DLayer::FollowWindowMinCell(Origin256, FIntPoint(141, 128), 256, 12) == FIntPoint(16, 0));
	TestTrue(TEXT("Thirteen cells away on y moves only y"), FTDFog3DLayer::FollowWindowMinCell(Origin256, FIntPoint(128, 115), 256, 12) == FIntPoint(0, -16));
	TestTrue(TEXT("Thirteen cells away on both axes moves both"), FTDFog3DLayer::FollowWindowMinCell(Origin256, FIntPoint(141, 115), 256, 12) == FIntPoint(16, -16));
	TestTrue(TEXT("Negative coordinates keep within twelve cells"), FTDFog3DLayer::FollowWindowMinCell(FIntPoint(-256, -256), FIntPoint(-140, -116), 256, 12) == FIntPoint(-256, -256));
	TestTrue(TEXT("Negative coordinates move at thirteen cells"), FTDFog3DLayer::FollowWindowMinCell(FIntPoint(-256, -256), FIntPoint(-141, -256 + 128), 256, 12) == FIntPoint(-272, -256));
	TestTrue(TEXT("A 128 cell window keeps at twelve cells"), FTDFog3DLayer::FollowWindowMinCell(FIntPoint(32, 32), FIntPoint(32 + 64 + 12, 32 + 64), 128, 12) == FIntPoint(32, 32));
	TestTrue(TEXT("A 128 cell window moves at thirteen cells"), FTDFog3DLayer::FollowWindowMinCell(FIntPoint(32, 32), FIntPoint(32 + 64 + 13, 32 + 64), 128, 12) == FIntPoint(48, 32));

	const FFollowReferenceCase ReferenceCases[] = {
		{ 128, { 16, -336 }, { 90, -306 }, { 16, -368 } },
		{ 128, { -496, 448 }, { -460, 518 }, { -528, 448 } },
		{ 128, { 544, -528 }, { 632, -477 }, { 576, -544 } },
		{ 128, { -576, -464 }, { -497, -387 }, { -560, -448 } },
		{ 128, { -512, -160 }, { -477, -66 }, { -544, -128 } },
		{ 128, { 224, -528 }, { 320, -489 }, { 256, -560 } },
		{ 128, { -192, 544 }, { -161, 641 }, { -224, 576 } },
		{ 128, { 544, 160 }, { 574, 212 }, { 512, 160 } },
		{ 128, { -560, 496 }, { -519, 557 }, { -576, 496 } },
		{ 128, { 208, -352 }, { 301, -313 }, { 240, -384 } },
		{ 256, { 528, -16 }, { 687, 95 }, { 560, -32 } },
		{ 256, { -432, 544 }, { -271, 656 }, { -400, 528 } },
		{ 256, { 112, -448 }, { 270, -352 }, { 144, -480 } },
		{ 256, { 512, -528 }, { 679, -414 }, { 544, -544 } },
		{ 256, { 368, 448 }, { 510, 576 }, { 384, 448 } },
		{ 256, { 304, 544 }, { 450, 678 }, { 320, 544 } },
		{ 256, { -32, -144 }, { 79, -25 }, { -48, -144 } },
		{ 256, { -480, 528 }, { -354, 683 }, { -480, 560 } },
		{ 256, { 368, 48 }, { 513, 172 }, { 384, 48 } },
		{ 256, { 592, -496 }, { 695, -343 }, { 560, -464 } },
	};
	for (const FFollowReferenceCase& Case : ReferenceCases)
	{
		const FIntPoint Actual = FTDFog3DLayer::FollowWindowMinCell(Case.Origin, Case.PlayerCell, Case.WindowCells, 12);
		TestTrue(
			FString::Printf(TEXT("Reference follow N %d origin %s player %s expected %s actual %s"), Case.WindowCells, *DescribeCell(Case.Origin), *DescribeCell(Case.PlayerCell), *DescribeCell(Case.Expected), *DescribeCell(Actual)),
			Actual == Case.Expected);
	}

	TestTrue(TEXT("A jump of the full window on x is too far"), FTDFog3DLayer::IsWindowJumpTooFar(FIntPoint(0, 0), FIntPoint(256, 0), 256));
	TestTrue(TEXT("A jump of the full window on y is too far"), FTDFog3DLayer::IsWindowJumpTooFar(FIntPoint(0, 0), FIntPoint(0, -256), 256));
	TestTrue(TEXT("A jump one step short is not too far"), !FTDFog3DLayer::IsWindowJumpTooFar(FIntPoint(0, 0), FIntPoint(240, 0), 256));
	TestTrue(TEXT("A diagonal jump short of the window is not too far"), !FTDFog3DLayer::IsWindowJumpTooFar(FIntPoint(-255, 255), FIntPoint(0, 0), 256));
	TestTrue(TEXT("A 128 cell window jumps at 128"), FTDFog3DLayer::IsWindowJumpTooFar(FIntPoint(0, 0), FIntPoint(128, 0), 128));
	TestTrue(TEXT("A 128 cell window does not jump at 112"), !FTDFog3DLayer::IsWindowJumpTooFar(FIntPoint(0, 0), FIntPoint(112, 0), 128));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLayerClockCadenceTest,
	"TDGame.FluidFX.Fog3D.ClockCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLayerClockCadenceTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DLayerTestPrivate;

	const FTDFog3DSettings Settings;
	const float StepEpsilon = 1.0e-5f;

	{
		FTDFog3DLayer Layer;
		Layer.Configure(Settings, 2, 2);
		TestNearlyEqual(TEXT("High quality steps per second"), Layer.GetStepsPerSecond(), 60.0f, 1.0e-4f);
		const FClockRun Run = RunClock(Layer, 60, 1);
		TestEqual(TEXT("Sixty steps per second at sixty frames per second"), Run.StepCount, 60);
		TestNearlyEqual(TEXT("Step delta is the frame time"), Run.MaximumStepDeltaSeconds, 1.0f / 60.0f, StepEpsilon);
		TestNearlyEqual(TEXT("Elapsed is the frame time"), Run.MaximumElapsedSeconds, 1.0f / 60.0f, StepEpsilon);
	}

	{
		FTDFog3DLayer Layer;
		Layer.Configure(Settings, 2, 2);
		const FClockRun Run = RunClock(Layer, 144, 1);
		TestTrue(*FString::Printf(TEXT("About sixty steps in one second at one hundred forty four frames per second (got %d)"), Run.StepCount), Run.StepCount >= 59 && Run.StepCount <= 60);
		TestNearlyEqual(TEXT("Shortest elapsed covers two frames"), Run.MinimumElapsedSeconds, 2.0f / 144.0f, StepEpsilon);
		TestNearlyEqual(TEXT("Longest elapsed covers three frames"), Run.MaximumElapsedSeconds, 3.0f / 144.0f, StepEpsilon);
		TestNearlyEqual(TEXT("Step delta covers at most three frames"), Run.MaximumStepDeltaSeconds, 3.0f / 144.0f, StepEpsilon);
	}

	{
		FTDFog3DLayer Layer;
		Layer.Configure(Settings, 2, 2);
		const FClockRun Run = RunClock(Layer, 144, 10);
		TestTrue(*FString::Printf(TEXT("Six hundred plus or minus two steps in ten seconds at 144 frames per second (got %d)"), Run.StepCount), FMath::Abs(Run.StepCount - 600) <= 2);
		TestTrue(*FString::Printf(TEXT("No one second span at 144 frames per second holds more than sixty one steps (got %d)"), Run.MaximumStepsInAnySecond), Run.MaximumStepsInAnySecond <= 61);
	}

	{
		FTDFog3DLayer Layer;
		Layer.Configure(Settings, 2, 2);
		const FClockRun Run = RunClock(Layer, 60, 10, 0.0005f);
		TestTrue(*FString::Printf(TEXT("A sixty hertz screen with half a millisecond of jitter keeps at least 570 steps in ten seconds (got %d)"), Run.StepCount), Run.StepCount >= 570);
		TestTrue(*FString::Printf(TEXT("A sixty hertz screen with jitter never exceeds 601 steps in ten seconds (got %d)"), Run.StepCount), Run.StepCount <= 601);
		TestTrue(*FString::Printf(TEXT("No one second span with jitter holds more than sixty one steps (got %d)"), Run.MaximumStepsInAnySecond), Run.MaximumStepsInAnySecond <= 61);
	}

	{
		FTDFog3DLayer Layer;
		Layer.Configure(Settings, 2, 2);
		const FClockRun Run = RunClock(Layer, 30, 1);
		TestEqual(TEXT("Every frame steps at thirty frames per second"), Run.StepCount, 30);
		TestNearlyEqual(TEXT("Step delta is thirty hertz"), Run.MaximumStepDeltaSeconds, 1.0f / 30.0f, StepEpsilon);
		TestNearlyEqual(TEXT("Elapsed is one frame at thirty hertz"), Run.MinimumElapsedSeconds, 1.0f / 30.0f, StepEpsilon);
	}

	{
		FTDFog3DLayer Layer;
		Layer.Configure(Settings, 2, 2);
		const FTDFog3DClockDecision Refused = Layer.AdvanceClock(1.0f / 60.0f);
		TestTrue(TEXT("The first tick steps"), Refused.bStepsNow);
		const FTDFog3DClockDecision Retried = Layer.AdvanceClock(1.0f / 60.0f);
		TestTrue(TEXT("A tick after an unsubmitted step steps again"), Retried.bStepsNow);
		TestNearlyEqual(TEXT("Elapsed after an unsubmitted step covers both ticks"), Retried.ElapsedSeconds, 2.0f / 60.0f, StepEpsilon);
		TestNearlyEqual(TEXT("Step delta after an unsubmitted step covers both ticks"), Retried.StepDeltaSeconds, 2.0f / 60.0f, StepEpsilon);
		FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, FTDFog3DStepRequest());
		const FTDFog3DClockDecision AfterSubmit = Layer.AdvanceClock(1.0f / 60.0f);
		TestNearlyEqual(TEXT("Elapsed after a submitted step is one tick"), AfterSubmit.ElapsedSeconds, 1.0f / 60.0f, StepEpsilon);
	}

	{
		FTDFog3DLayer Layer;
		Layer.Configure(Settings, 2, 2);
		const float FastFrameSeconds = 1.0f / 144.0f;
		int32 FramesUntilStep = 0;
		FTDFog3DClockDecision Decision;
		while (FramesUntilStep < 10 && !Decision.bStepsNow)
		{
			Decision = Layer.AdvanceClock(FastFrameSeconds);
			++FramesUntilStep;
		}
		TestTrue(TEXT("A decision to step is reached at 144 frames per second"), Decision.bStepsNow);
		const FTDFog3DClockDecision Undecided = Layer.AdvanceClock(FastFrameSeconds);
		TestTrue(TEXT("Deciding to step without submitting keeps the step credit so the next tick still steps"), Undecided.bStepsNow);
		TestNearlyEqual(TEXT("The elapsed time keeps growing until a step is submitted"), Undecided.ElapsedSeconds, static_cast<float>(FramesUntilStep + 1) / 144.0f, StepEpsilon);
	}

	{
		FTDFog3DLayer Layer;
		Layer.Configure(Settings, 2, 2);
		const float FastFrameSeconds = 1.0f / 144.0f;
		int32 WarmUpFrames = 0;
		while (WarmUpFrames < 10 && !Layer.AdvanceClock(FastFrameSeconds).bStepsNow)
		{
			++WarmUpFrames;
		}
		FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, FTDFog3DStepRequest());
		const FTDFog3DClockDecision Hitch = Layer.AdvanceClock(0.5f);
		TestTrue(TEXT("A half second hitch steps"), Hitch.bStepsNow);
		TestNearlyEqual(TEXT("The hitch step delta is capped at 0.05"), Hitch.StepDeltaSeconds, 0.05f, 1.0e-6f);
		TestNearlyEqual(TEXT("The hitch elapsed is the whole half second"), Hitch.ElapsedSeconds, 0.5f, 1.0e-6f);
		FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, FTDFog3DStepRequest());
		const int32 CatchUpSteps = CountStepsOverFrames(Layer, FastFrameSeconds, 2);
		TestTrue(*FString::Printf(TEXT("After the hitch the next two frames step at most once (got %d)"), CatchUpSteps), CatchUpSteps <= 1);
	}

	for (const int32 QualityLevel : { 0, 1 })
	{
		FTDFog3DLayer Layer;
		Layer.Configure(Settings, QualityLevel, 2);
		TestNearlyEqual(TEXT("Low and medium steps per second"), Layer.GetStepsPerSecond(), 30.0f, 1.0e-4f);
		const FClockRun Run = RunClock(Layer, 60, 1);
		TestEqual(TEXT("Thirty steps per second at sixty frames per second"), Run.StepCount, 30);
		TestNearlyEqual(TEXT("Elapsed covers two frames"), Run.MinimumElapsedSeconds, 2.0f / 60.0f, StepEpsilon);
	}

	{
		FTDFog3DLayer Layer;
		Layer.Configure(Settings, 2, 2);
		const FTDFog3DClockDecision Hitch = Layer.AdvanceClock(0.5f);
		TestTrue(TEXT("A long frame steps"), Hitch.bStepsNow);
		TestNearlyEqual(TEXT("Step delta is capped at one twentieth"), Hitch.StepDeltaSeconds, 0.05f, 1.0e-6f);
		TestNearlyEqual(TEXT("Elapsed is not capped"), Hitch.ElapsedSeconds, 0.5f, 1.0e-6f);
	}

	{
		FTDFog3DLayer Layer;
		Layer.Configure(Settings, 2, 2);
		const FClockRun Run = RunClock(Layer, 10, 1);
		TestEqual(TEXT("A slow screen steps every frame"), Run.StepCount, 10);
		TestNearlyEqual(TEXT("A slow screen caps step delta"), Run.MaximumStepDeltaSeconds, 0.05f, 1.0e-6f);
		TestNearlyEqual(TEXT("A slow screen keeps the real elapsed time"), Run.MinimumElapsedSeconds, 0.1f, 1.0e-5f);
	}

	{
		FTDFog3DLayer Layer;
		Layer.Configure(Settings, 2, 2);
		TestTrue(TEXT("Invalid frame time does not step"), !Layer.AdvanceClock(0.0f).bStepsNow);
		TestTrue(TEXT("Negative frame time does not step"), !Layer.AdvanceClock(-1.0f).bStepsNow);
		TestTrue(TEXT("Not a number does not step"), !Layer.AdvanceClock(std::numeric_limits<float>::quiet_NaN()).bStepsNow);
		TestTrue(TEXT("One short frame does not step at sixty"), !Layer.AdvanceClock(0.01f).bStepsNow);
		TestTrue(TEXT("The remaining time steps"), Layer.AdvanceClock(0.0067f).bStepsNow);
	}

	TestTrue(TEXT("Just below the tolerance does not step"), !FTDFog3DLayer::DecideClock(0.0166, 0.0166, 60.0f, 0.05f).bStepsNow);
	TestTrue(TEXT("At the interval steps"), FTDFog3DLayer::DecideClock(1.0 / 60.0, 1.0 / 60.0, 60.0f, 0.05f).bStepsNow);
	TestTrue(TEXT("Zero steps per second never steps"), !FTDFog3DLayer::DecideClock(1.0, 1.0, 0.0f, 0.05f).bStepsNow);
	const double SixtyHertzIntervalSeconds = 1.0 / 60.0;
	TestTrue(TEXT("Credit within the half tolerance of the interval steps"), FTDFog3DLayer::DecideClock(SixtyHertzIntervalSeconds * (1.0 - 5.0e-4), 1.0, 60.0f, 0.05f).bStepsNow);
	TestTrue(TEXT("Credit beyond twice the tolerance of the interval does not step"), !FTDFog3DLayer::DecideClock(SixtyHertzIntervalSeconds * (1.0 - 2.0e-3), 1.0, 60.0f, 0.05f).bStepsNow);
	TestTrue(TEXT("Zero maximum step never steps"), !FTDFog3DLayer::DecideClock(1.0, 1.0, 60.0f, 0.0f).bStepsNow);
	TestTrue(TEXT("No time never steps"), !FTDFog3DLayer::DecideClock(0.0, 0.0, 60.0f, 0.05f).bStepsNow);
	TestTrue(TEXT("Credit below the interval does not step even after a long wait"), !FTDFog3DLayer::DecideClock(0.01, 0.5, 60.0f, 0.05f).bStepsNow);
	TestTrue(TEXT("Credit without elapsed time does not step"), !FTDFog3DLayer::DecideClock(1.0 / 60.0, 0.0, 60.0f, 0.05f).bStepsNow);
	const FTDFog3DClockDecision LongWait = FTDFog3DLayer::DecideClock(1.0 / 60.0, 0.5, 60.0f, 0.05f);
	TestTrue(TEXT("Credit at the interval steps after a long wait"), LongWait.bStepsNow);
	TestNearlyEqual(TEXT("Step delta uses the time since the submitted step, capped"), LongWait.StepDeltaSeconds, 0.05f, 1.0e-6f);
	TestNearlyEqual(TEXT("Elapsed uses the time since the submitted step"), LongWait.ElapsedSeconds, 0.5f, 1.0e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLayerModeAndQualityResolveTest,
	"TDGame.FluidFX.Fog3D.ModeAndQualityResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLayerModeAndQualityResolveTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Console minus one uses data mode 2"), FTDFog3DLayer::ResolveMode(-1, 2), 2);
	TestEqual(TEXT("Console minus one uses data mode 0"), FTDFog3DLayer::ResolveMode(-1, 0), 0);
	TestEqual(TEXT("Console 0 overrides data"), FTDFog3DLayer::ResolveMode(0, 2), 0);
	TestEqual(TEXT("Console 1 overrides data"), FTDFog3DLayer::ResolveMode(1, 2), 1);
	TestEqual(TEXT("Console 2 overrides data"), FTDFog3DLayer::ResolveMode(2, 0), 2);
	TestEqual(TEXT("Console 9 clamps to 2"), FTDFog3DLayer::ResolveMode(9, 1), 2);
	TestEqual(TEXT("Data above range clamps"), FTDFog3DLayer::ResolveMode(-1, 7), 2);

	TestEqual(TEXT("Console minus one uses data quality 2"), FTDFog3DLayer::ResolveQualityLevel(-1, 2), 2);
	TestEqual(TEXT("Console minus one uses data quality 1"), FTDFog3DLayer::ResolveQualityLevel(-1, 1), 1);
	TestEqual(TEXT("Console 0 overrides data quality"), FTDFog3DLayer::ResolveQualityLevel(0, 2), 0);
	TestEqual(TEXT("Console 1 overrides data quality"), FTDFog3DLayer::ResolveQualityLevel(1, 2), 1);
	TestEqual(TEXT("Console 2 overrides data quality"), FTDFog3DLayer::ResolveQualityLevel(2, 0), 2);
	TestEqual(TEXT("Console 9 clamps quality to 2"), FTDFog3DLayer::ResolveQualityLevel(9, 0), 2);
	TestEqual(TEXT("Data quality above range clamps"), FTDFog3DLayer::ResolveQualityLevel(-1, 5), 2);

	const FTDFog3DSettings Settings;
	FTDFog3DLayer Layer;
	Layer.Configure(Settings, 0, 2);
	TestEqual(TEXT("Low quality resolution"), Layer.GetResolution(), 128);
	TestNearlyEqual(TEXT("Low quality steps"), Layer.GetStepsPerSecond(), 30.0f, 1.0e-4f);
	Layer.Configure(Settings, 1, 2);
	TestEqual(TEXT("Medium quality resolution"), Layer.GetResolution(), 256);
	TestNearlyEqual(TEXT("Medium quality steps"), Layer.GetStepsPerSecond(), 30.0f, 1.0e-4f);
	Layer.Configure(Settings, 2, 1);
	TestEqual(TEXT("High quality resolution"), Layer.GetResolution(), 256);
	TestNearlyEqual(TEXT("High quality steps"), Layer.GetStepsPerSecond(), 60.0f, 1.0e-4f);
	TestEqual(TEXT("Mode is kept"), Layer.GetMode(), 1);
	Layer.Configure(Settings, 9, 9);
	TestEqual(TEXT("Mode above range clamps"), Layer.GetMode(), 2);
	TestEqual(TEXT("Quality above range clamps to high"), Layer.GetResolution(), 256);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLayerSpherePackingTest,
	"TDGame.FluidFX.Fog3D.SpherePacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLayerSpherePackingTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DLayerTestPrivate;

	const FVector2D WindowOrigin(750.0, 1750.0);
	const double FloorZ = 10.0;
	TMap<int32, FVector> Previous;
	Previous.Add(0, FVector(900.0, 2000.0, 50.0));

	TArray<FTDFog3DInputSample> Inputs;
	Inputs.Add(MakeSample(0, FVector(1000.0, 2000.0, 50.0), 20.0f, 1.5f, 30.0f));

	TArray<FTDFluidGpuFog3DSphere> Spheres;
	int32 Dropped = -1;
	FTDFog3DLayer::PackSpheres(Inputs, Previous, WindowOrigin, FloorZ, false, 0.1f, false, 3000.0f, 16, Spheres, Dropped);
	TestEqual(TEXT("One sphere"), Spheres.Num(), 1);
	TestEqual(TEXT("Nothing dropped"), Dropped, 0);
	if (Spheres.Num() == 1)
	{
		const FTDFluidGpuFog3DSphere& Sphere = Spheres[0];
		TestNearlyEqual(TEXT("Start x is the previous position relative to the window"), Sphere.StartCmRadiusCm.X, 150.0f, 1.0e-3f);
		TestNearlyEqual(TEXT("Start y"), Sphere.StartCmRadiusCm.Y, 250.0f, 1.0e-3f);
		TestNearlyEqual(TEXT("Start z is relative to the floor"), Sphere.StartCmRadiusCm.Z, 40.0f, 1.0e-3f);
		TestNearlyEqual(TEXT("Radius"), Sphere.StartCmRadiusCm.W, 20.0f, 1.0e-6f);
		TestNearlyEqual(TEXT("End x"), Sphere.EndCmWindStrength.X, 250.0f, 1.0e-3f);
		TestNearlyEqual(TEXT("End y"), Sphere.EndCmWindStrength.Y, 250.0f, 1.0e-3f);
		TestNearlyEqual(TEXT("End z"), Sphere.EndCmWindStrength.Z, 40.0f, 1.0e-3f);
		TestNearlyEqual(TEXT("Wind strength"), Sphere.EndCmWindStrength.W, 1.5f, 1.0e-6f);
		TestNearlyEqual(TEXT("Velocity x uses the elapsed time"), Sphere.VelocityCmPerSecErosionPerSec.X, 1000.0f, 0.01f);
		TestNearlyEqual(TEXT("Velocity y"), Sphere.VelocityCmPerSecErosionPerSec.Y, 0.0f, 0.01f);
		TestNearlyEqual(TEXT("Velocity z"), Sphere.VelocityCmPerSecErosionPerSec.Z, 0.0f, 0.01f);
		TestNearlyEqual(TEXT("Erosion"), Sphere.VelocityCmPerSecErosionPerSec.W, 30.0f, 1.0e-6f);
	}

	TArray<FTDFluidGpuFog3DSphere> ResetSpheres;
	FTDFog3DLayer::PackSpheres(Inputs, Previous, WindowOrigin, FloorZ, true, 0.1f, false, 3000.0f, 16, ResetSpheres, Dropped);
	if (ResetSpheres.Num() == 1)
	{
		TestTrue(TEXT("A reset step starts where it ends"), ResetSpheres[0].StartCmRadiusCm.X == ResetSpheres[0].EndCmWindStrength.X && ResetSpheres[0].StartCmRadiusCm.Y == ResetSpheres[0].EndCmWindStrength.Y && ResetSpheres[0].StartCmRadiusCm.Z == ResetSpheres[0].EndCmWindStrength.Z);
		TestTrue(TEXT("A reset step has no velocity"), ResetSpheres[0].VelocityCmPerSecErosionPerSec.X == 0.0f && ResetSpheres[0].VelocityCmPerSecErosionPerSec.Y == 0.0f && ResetSpheres[0].VelocityCmPerSecErosionPerSec.Z == 0.0f);
		TestNearlyEqual(TEXT("A reset step keeps erosion"), ResetSpheres[0].VelocityCmPerSecErosionPerSec.W, 30.0f, 1.0e-6f);
	}
	else
	{
		AddError(TEXT("Reset packing must keep the sphere"));
	}

	TArray<FTDFog3DInputSample> NewSlotInputs;
	NewSlotInputs.Add(MakeSample(7, FVector(1000.0, 2000.0, 50.0)));
	TArray<FTDFluidGpuFog3DSphere> NewSlotSpheres;
	FTDFog3DLayer::PackSpheres(NewSlotInputs, Previous, WindowOrigin, FloorZ, false, 0.1f, false, 3000.0f, 16, NewSlotSpheres, Dropped);
	if (NewSlotSpheres.Num() == 1)
	{
		TestTrue(TEXT("A new slot starts where it ends"), NewSlotSpheres[0].StartCmRadiusCm.X == NewSlotSpheres[0].EndCmWindStrength.X && NewSlotSpheres[0].StartCmRadiusCm.Z == NewSlotSpheres[0].EndCmWindStrength.Z);
		TestTrue(TEXT("A new slot has no velocity"), NewSlotSpheres[0].VelocityCmPerSecErosionPerSec.X == 0.0f);
	}
	else
	{
		AddError(TEXT("New slot packing must keep the sphere"));
	}

	TMap<int32, FVector> FastPrevious;
	FastPrevious.Add(0, FVector(700.0, 1600.0, 50.0));
	TArray<FTDFluidGpuFog3DSphere> FastSpheres;
	FTDFog3DLayer::PackSpheres(Inputs, FastPrevious, WindowOrigin, FloorZ, false, 0.1f, false, 3000.0f, 16, FastSpheres, Dropped);
	if (FastSpheres.Num() == 1)
	{
		const FVector4f& Velocity = FastSpheres[0].VelocityCmPerSecErosionPerSec;
		TestNearlyEqual(TEXT("Fifty meters per second is cut to thirty with the same direction in x"), Velocity.X, 1800.0f, 0.1f);
		TestNearlyEqual(TEXT("Same direction in y"), Velocity.Y, 2400.0f, 0.1f);
		TestNearlyEqual(TEXT("Speed is the limit"), FVector3f(Velocity.X, Velocity.Y, Velocity.Z).Size(), 3000.0f, 0.1f);
	}
	else
	{
		AddError(TEXT("Fast packing must keep the sphere"));
	}

	TArray<FTDFluidGpuFog3DSphere> NoTimeSpheres;
	FTDFog3DLayer::PackSpheres(Inputs, Previous, WindowOrigin, FloorZ, false, 0.0f, false, 3000.0f, 16, NoTimeSpheres, Dropped);
	if (NoTimeSpheres.Num() == 1)
	{
		TestTrue(TEXT("No elapsed time gives no velocity"), NoTimeSpheres[0].VelocityCmPerSecErosionPerSec.X == 0.0f && NoTimeSpheres[0].VelocityCmPerSecErosionPerSec.Y == 0.0f && NoTimeSpheres[0].VelocityCmPerSecErosionPerSec.Z == 0.0f);
		TestNearlyEqual(TEXT("No elapsed time still keeps the end position"), NoTimeSpheres[0].EndCmWindStrength.X, 250.0f, 1.0e-3f);
	}
	else
	{
		AddError(TEXT("No elapsed time packing must keep the sphere"));
	}

	TArray<FTDFog3DInputSample> ManyInputs;
	for (int32 InputIndex = 0; InputIndex < 17; ++InputIndex)
	{
		ManyInputs.Add(MakeSample(InputIndex, FVector(800.0 + 10.0 * InputIndex, 1800.0, 30.0), 20.0f + InputIndex));
	}
	TArray<FTDFluidGpuFog3DSphere> ManySpheres;
	FTDFog3DLayer::PackSpheres(ManyInputs, TMap<int32, FVector>(), WindowOrigin, FloorZ, false, 0.05f, false, 3000.0f, 16, ManySpheres, Dropped);
	TestEqual(TEXT("Seventeen inputs pack sixteen spheres"), ManySpheres.Num(), 16);
	TestEqual(TEXT("One input is dropped"), Dropped, 1);
	for (int32 SphereIndex = 0; SphereIndex < ManySpheres.Num(); ++SphereIndex)
	{
		if (FMath::Abs(ManySpheres[SphereIndex].StartCmRadiusCm.W - (20.0f + SphereIndex)) > 1.0e-4f)
		{
			AddError(FString::Printf(TEXT("Sphere %d is out of collection order"), SphereIndex));
			break;
		}
	}

	TArray<FTDFluidGpuFog3DSphere> LimitedSpheres;
	FTDFog3DLayer::PackSpheres(ManyInputs, TMap<int32, FVector>(), WindowOrigin, FloorZ, false, 0.05f, false, 3000.0f, 4, LimitedSpheres, Dropped);
	TestEqual(TEXT("The configured limit packs four"), LimitedSpheres.Num(), 4);
	TestEqual(TEXT("Thirteen dropped"), Dropped, 13);

	TArray<FTDFluidGpuFog3DSphere> OverLimitSpheres;
	FTDFog3DLayer::PackSpheres(ManyInputs, TMap<int32, FVector>(), WindowOrigin, FloorZ, false, 0.05f, false, 3000.0f, 99, OverLimitSpheres, Dropped);
	TestEqual(TEXT("The module limit caps the configured limit"), OverLimitSpheres.Num(), 16);

	TArray<FTDFog3DInputSample> InvalidInputs;
	InvalidInputs.Add(MakeSample(0, FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0)));
	InvalidInputs.Add(MakeSample(1, FVector(900.0, 1900.0, 20.0), 0.0f));
	InvalidInputs.Add(MakeSample(2, FVector(900.0, 1900.0, 20.0), 25.0f));
	TArray<FTDFluidGpuFog3DSphere> ValidSpheres;
	FTDFog3DLayer::PackSpheres(InvalidInputs, TMap<int32, FVector>(), WindowOrigin, FloorZ, false, 0.05f, false, 3000.0f, 16, ValidSpheres, Dropped);
	TestEqual(TEXT("Invalid inputs are skipped"), ValidSpheres.Num(), 1);
	TestEqual(TEXT("Skipped inputs are not counted as dropped"), Dropped, 0);

	const FVector LargeShift(1.0e7, -1.0e7, 0.0);
	TMap<int32, FVector> LargePrevious;
	LargePrevious.Add(0, Previous[0] + LargeShift);
	TArray<FTDFog3DInputSample> LargeInputs;
	LargeInputs.Add(MakeSample(0, Inputs[0].CurrentWorld + LargeShift, 20.0f, 1.5f, 30.0f));
	TArray<FTDFluidGpuFog3DSphere> LargeSpheres;
	FTDFog3DLayer::PackSpheres(LargeInputs, LargePrevious, WindowOrigin + FVector2D(LargeShift.X, LargeShift.Y), FloorZ, false, 0.1f, false, 3000.0f, 16, LargeSpheres, Dropped);
	if (LargeSpheres.Num() == 1 && Spheres.Num() == 1)
	{
		TestTrue(TEXT("A world coordinate of ten million centimeters packs the same window values"), AreSpheresEqual(LargeSpheres[0], Spheres[0]));
	}
	else
	{
		AddError(TEXT("Large coordinate packing must keep the sphere"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLayerSpherePackingWindGainTest,
	"TDGame.FluidFX.Fog3D.SpherePackingWindGain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLayerSpherePackingWindGainTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DLayerTestPrivate;

	const FVector2D WindowOrigin(750.0, 1750.0);
	const double FloorZ = 10.0;
	const FVector CurrentWorld(1000.0, 2000.0, 50.0);
	TMap<int32, FVector> Previous;
	Previous.Add(0, CurrentWorld - FVector(100.0, 50.0, 10.0));

	TArray<FTDFog3DInputSample> PlainInputs;
	PlainInputs.Add(MakeSample(0, CurrentWorld, 20.0f, 3.0f, 30.0f));
	TArray<FTDFog3DInputSample> GainedInputs = PlainInputs;
	GainedInputs[0].WindGain = 1.25f;

	TArray<FTDFluidGpuFog3DSphere> PlainSpheres;
	TArray<FTDFluidGpuFog3DSphere> GainedOffSpheres;
	TArray<FTDFluidGpuFog3DSphere> GainedOnSpheres;
	int32 Dropped = -1;
	FTDFog3DLayer::PackSpheres(PlainInputs, Previous, WindowOrigin, FloorZ, false, 0.1f, false, 3000.0f, 16, PlainSpheres, Dropped);
	FTDFog3DLayer::PackSpheres(GainedInputs, Previous, WindowOrigin, FloorZ, false, 0.1f, false, 3000.0f, 16, GainedOffSpheres, Dropped);
	FTDFog3DLayer::PackSpheres(GainedInputs, Previous, WindowOrigin, FloorZ, false, 0.1f, true, 3000.0f, 16, GainedOnSpheres, Dropped);
	if (!TestTrue(TEXT("Each packing keeps the sphere"), PlainSpheres.Num() == 1 && GainedOffSpheres.Num() == 1 && GainedOnSpheres.Num() == 1))
	{
		return false;
	}
	TestTrue(TEXT("With the switch off a gain of 1.25 packs the same bytes as a gain of 1"), FMemory::Memcmp(&GainedOffSpheres[0], &PlainSpheres[0], sizeof(FTDFluidGpuFog3DSphere)) == 0);

	const auto IsWithinRelative = [](double Actual, double Expected, double RelativeTolerance)
	{
		return FMath::Abs(Actual - Expected) <= RelativeTolerance * FMath::Abs(Expected);
	};
	const FVector4f& PlainVelocity = PlainSpheres[0].VelocityCmPerSecErosionPerSec;
	const FVector4f& GainedVelocity = GainedOnSpheres[0].VelocityCmPerSecErosionPerSec;
	TestTrue(TEXT("The plain velocity has three nonzero components"), PlainVelocity.X != 0.0f && PlainVelocity.Y != 0.0f && PlainVelocity.Z != 0.0f);
	TestTrue(TEXT("With the switch on velocity x is 1.25 times the plain velocity"), IsWithinRelative(GainedVelocity.X, 1.25 * PlainVelocity.X, 1.0e-6));
	TestTrue(TEXT("With the switch on velocity y is 1.25 times the plain velocity"), IsWithinRelative(GainedVelocity.Y, 1.25 * PlainVelocity.Y, 1.0e-6));
	TestTrue(TEXT("With the switch on velocity z is 1.25 times the plain velocity"), IsWithinRelative(GainedVelocity.Z, 1.25 * PlainVelocity.Z, 1.0e-6));
	TestTrue(TEXT("The gain leaves positions and radius alone"), GainedOnSpheres[0].StartCmRadiusCm == PlainSpheres[0].StartCmRadiusCm && GainedOnSpheres[0].EndCmWindStrength == PlainSpheres[0].EndCmWindStrength);
	TestEqual(TEXT("The gain leaves erosion alone"), GainedVelocity.W, PlainVelocity.W);

	TMap<int32, FVector> FastPrevious;
	FastPrevious.Add(0, CurrentWorld - FVector(120.0, 160.0, 0.0));
	TArray<FTDFog3DInputSample> DoubledInputs = PlainInputs;
	DoubledInputs[0].WindGain = 2.0f;
	TArray<FTDFluidGpuFog3DSphere> FastPlainSpheres;
	TArray<FTDFluidGpuFog3DSphere> DoubledSpheres;
	FTDFog3DLayer::PackSpheres(PlainInputs, FastPrevious, WindowOrigin, FloorZ, false, 0.1f, true, 3000.0f, 16, FastPlainSpheres, Dropped);
	FTDFog3DLayer::PackSpheres(DoubledInputs, FastPrevious, WindowOrigin, FloorZ, false, 0.1f, true, 3000.0f, 16, DoubledSpheres, Dropped);
	if (!TestTrue(TEXT("Fast packings keep the sphere"), FastPlainSpheres.Num() == 1 && DoubledSpheres.Num() == 1))
	{
		return false;
	}
	const FVector4f& FastPlainVelocity = FastPlainSpheres[0].VelocityCmPerSecErosionPerSec;
	const FVector4f& DoubledVelocity = DoubledSpheres[0].VelocityCmPerSecErosionPerSec;
	TestNearlyEqual(TEXT("The fast input moves at 2000 cm/s before the gain"), FVector3f(FastPlainVelocity.X, FastPlainVelocity.Y, FastPlainVelocity.Z).Size(), 2000.0f, 1.0e-3f);
	TestNearlyEqual(TEXT("A gain of 2 on 2000 cm/s is cut to the 3000 cm/s limit after the gain"), FVector3f(DoubledVelocity.X, DoubledVelocity.Y, DoubledVelocity.Z).Size(), 3000.0f, 1.0e-3f);
	TestNearlyEqual(TEXT("The cut keeps the direction in x"), DoubledVelocity.X, 1800.0f, 1.0e-3f);
	TestNearlyEqual(TEXT("The cut keeps the direction in y"), DoubledVelocity.Y, 2400.0f, 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLayerBladeSpheresTest,
	"TDGame.FluidFX.Fog3D.BladeSpheres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLayerBladeSpheresTest::RunTest(const FString& Parameters)
{
	const FTDFog3DSettings Settings;
	const double BladeLength = FVector::Dist(Settings.BladeBaseLocalCm, Settings.BladeTipLocalCm);
	TestNearlyEqual(TEXT("The default blade is about 96 centimeters"), static_cast<float>(BladeLength), 96.14f, 0.05f);
	TestEqual(TEXT("The default blade needs four spheres"), FTDFog3DLayer::ComputeBladeSphereCount(BladeLength, Settings.BladeRadiusCm), 4);
	TestEqual(TEXT("One meter with twenty centimeter spheres needs six"), FTDFog3DLayer::ComputeBladeSphereCount(100.0, 20.0), 6);
	TestEqual(TEXT("A larger radius needs fewer spheres"), FTDFog3DLayer::ComputeBladeSphereCount(96.1, 40.0), 4);
	TestEqual(TEXT("A zero length blade has no spheres"), FTDFog3DLayer::ComputeBladeSphereCount(0.0, 20.0), 0);
	TestEqual(TEXT("A zero radius has no spheres"), FTDFog3DLayer::ComputeBladeSphereCount(96.1, 0.0), 0);

	const FTransform SocketTransform(FRotator(10.0, 20.0, 30.0), FVector(500.0, -300.0, 40.0));
	const FVector BaseWorld = SocketTransform.TransformPosition(Settings.BladeBaseLocalCm);
	const FVector TipWorld = SocketTransform.TransformPosition(Settings.BladeTipLocalCm);
	TArray<FVector> Centers;
	FTDFog3DLayer::ComputeBladeSphereCenters(BaseWorld, TipWorld, Settings.BladeRadiusCm, Centers);
	TestEqual(TEXT("Four centers"), Centers.Num(), 4);
	if (Centers.Num() != 4)
	{
		return false;
	}

	TestTrue(TEXT("The first sphere is at the tip"), Centers[0].Equals(TipWorld, 1.0e-6));
	TestTrue(TEXT("The last sphere is at the base"), Centers[3].Equals(BaseWorld, 1.0e-6));
	for (int32 CenterIndex = 1; CenterIndex < Centers.Num(); ++CenterIndex)
	{
		const double Spacing = FVector::Dist(Centers[CenterIndex - 1], Centers[CenterIndex]);
		TestTrue(FString::Printf(TEXT("Spacing %d is within one radius (%f)"), CenterIndex, Spacing), Spacing <= Settings.BladeRadiusCm + 1.0e-6);
	}

	TArray<FVector> Empty;
	FTDFog3DLayer::ComputeBladeSphereCenters(BaseWorld, BaseWorld, 20.0, Empty);
	TestEqual(TEXT("A zero length blade gives no centers"), Empty.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLayerObstacleSelectAndPackTest,
	"TDGame.FluidFX.Fog3D.ObstacleSelectAndPack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLayerObstacleSelectAndPackTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DLayerTestPrivate;

	const FTDFluidGpuFog3DInflation Inflation;
	double YawDegrees = 0.0;

	const FTDFluidSdfObstacle Upright = MakeUprightBox(FVector::ZeroVector, FVector(50.0), 30.0);
	TestTrue(TEXT("An upright box is accepted"), FTDFog3DLayer::ReadUprightYaw(Upright, YawDegrees));
	TestNearlyEqual(TEXT("Yaw of an upright box"), static_cast<float>(YawDegrees), 30.0f, 1.0e-4f);

	const FTDFluidSdfObstacle Flipped = UTDFluidObstacleComponent::MakeOrientedBoxObstacle(FVector::ZeroVector, FVector(50.0), FRotator(0.0, 30.0, 180.0));
	TestTrue(TEXT("A flipped box is accepted"), FTDFog3DLayer::ReadUprightYaw(Flipped, YawDegrees));
	TestNearlyEqual(TEXT("Yaw of a flipped box is the same box"), static_cast<float>(YawDegrees), 30.0f, 1.0e-3f);

	const FTDFluidSdfObstacle Tilted = UTDFluidObstacleComponent::MakeOrientedBoxObstacle(FVector::ZeroVector, FVector(50.0), FRotator(FMath::RadiansToDegrees(0.02), 0.0, 0.0));
	TestTrue(TEXT("A box tilted by 0.02 radians is skipped"), !FTDFog3DLayer::ReadUprightYaw(Tilted, YawDegrees));
	const FTDFluidSdfObstacle SlightlyTilted = UTDFluidObstacleComponent::MakeOrientedBoxObstacle(FVector::ZeroVector, FVector(50.0), FRotator(FMath::RadiansToDegrees(0.005), 0.0, 0.0));
	TestTrue(TEXT("A box tilted by 0.005 radians is accepted"), FTDFog3DLayer::ReadUprightYaw(SlightlyTilted, YawDegrees));
	const FTDFluidSdfObstacle SidewaysBox = UTDFluidObstacleComponent::MakeOrientedBoxObstacle(FVector::ZeroVector, FVector(50.0), FRotator(0.0, 0.0, 90.0));
	TestTrue(TEXT("A box lying on its side is skipped"), !FTDFog3DLayer::ReadUprightYaw(SidewaysBox, YawDegrees));
	FTDFluidSdfObstacle Sphere;
	Sphere.bIsBox = false;
	TestTrue(TEXT("A sphere is skipped"), !FTDFog3DLayer::ReadUprightYaw(Sphere, YawDegrees));

	const FVector2D WindowOrigin(-2000.0, -2000.0);
	const FVector2D WindowCenter(0.0, 0.0);
	const double FloorZ = 100.0;

	TArray<FTDFluidSdfObstacle> Registered;
	const TArray<FVector2D> TieOffsets = {
		FVector2D(5000.0, 0.0), FVector2D(0.0, 5000.0), FVector2D(-5000.0, 0.0), FVector2D(0.0, -5000.0),
		FVector2D(3000.0, 4000.0), FVector2D(-4000.0, 3000.0), FVector2D(-3000.0, -4000.0), FVector2D(4000.0, -3000.0),
		FVector2D(4000.0, 3000.0), FVector2D(-3000.0, 4000.0) };
	TArray<FVector> TieCenters;
	for (const FVector2D& TieOffset : TieOffsets)
	{
		const FVector Center(TieOffset.X, TieOffset.Y, 100.0);
		TestTrue(TEXT("Every tie candidate is at exactly the same distance from the window center"), FVector2D::DistSquared(TieOffset, WindowCenter) == 25000000.0);
		TieCenters.Add(Center);
		Registered.Add(MakeUprightBox(Center, FVector(50.0, 50.0, 50.0), 0.0));
	}
	TArray<FVector> NearCenters;
	for (int32 NearIndex = 0; NearIndex < 30; ++NearIndex)
	{
		const FVector Center(100.0 * (NearIndex + 1), 0.0, 100.0);
		NearCenters.Add(Center);
		Registered.Add(MakeUprightBox(Center, FVector(20.0, 20.0, 20.0), 0.0));
	}
	Registered.Add(Tilted);
	Registered.Add(Sphere);

	TArray<FTDFluidGpuFog3DObstacle> Packed;
	TArray<FTDFog3DObstacleKey> Keys;
	int32 Skipped = -1;
	FTDFog3DLayer::SelectAndPackObstacles(Registered, WindowOrigin, WindowCenter, FloorZ, Inflation, 32, Packed, Keys, Skipped);
	TestEqual(TEXT("Forty boxes select thirty two"), Keys.Num(), 32);
	TestEqual(TEXT("Packed count matches key count"), Packed.Num(), Keys.Num());
	TestEqual(TEXT("The tilted box and the sphere are skipped"), Skipped, 2);
	if (Keys.Num() == 32 && Packed.Num() == 32)
	{
		bool bNearOrderIsCorrect = true;
		for (int32 NearIndex = 0; NearIndex < 30; ++NearIndex)
		{
			bNearOrderIsCorrect = bNearOrderIsCorrect && Keys[NearIndex].WorldCenter.Equals(NearCenters[NearIndex], 1.0e-9);
		}
		TestTrue(TEXT("The nearest thirty come first in distance order"), bNearOrderIsCorrect);
		TestTrue(TEXT("Equal distances keep registration order (first)"), Keys[30].WorldCenter.Equals(TieCenters[0], 1.0e-9));
		TestTrue(TEXT("Equal distances keep registration order (second)"), Keys[31].WorldCenter.Equals(TieCenters[1], 1.0e-9));
		TestNearlyEqual(TEXT("Packed center x is relative to the window origin"), Packed[0].CenterCmYawCosine.X, 2100.0f, 1.0e-3f);
		TestNearlyEqual(TEXT("Packed center y"), Packed[0].CenterCmYawCosine.Y, 2000.0f, 1.0e-3f);
		TestNearlyEqual(TEXT("Packed center z is relative to the floor"), Packed[0].CenterCmYawCosine.Z, 0.0f, 1.0e-3f);
		TestNearlyEqual(TEXT("Packed yaw cosine"), Packed[0].CenterCmYawCosine.W, 1.0f, 1.0e-6f);
		TestNearlyEqual(TEXT("Packed yaw sine"), Packed[0].HalfExtentCmYawSine.W, 0.0f, 1.0e-6f);
		TestNearlyEqual(TEXT("A box wider than the inflation keeps its half width"), Packed[0].HalfExtentCmYawSine.X, 20.0f, 1.0e-4f);
		TestNearlyEqual(TEXT("A box taller than the inflation keeps its half height"), Packed[0].HalfExtentCmYawSine.Z, 20.0f, 1.0e-4f);
	}

	TArray<FTDFluidGpuFog3DObstacle> ThinPacked;
	TArray<FTDFog3DObstacleKey> ThinKeys;
	TArray<FTDFluidSdfObstacle> ThinRegistered;
	ThinRegistered.Add(MakeUprightBox(FVector(0.0, 0.0, 100.0), FVector(2.0, 2.0, 2.0), 90.0));
	FTDFog3DLayer::SelectAndPackObstacles(ThinRegistered, WindowOrigin, WindowCenter, FloorZ, Inflation, 32, ThinPacked, ThinKeys, Skipped);
	if (ThinPacked.Num() == 1 && ThinKeys.Num() == 1)
	{
		const FVector ThinWindowOffset(WindowOrigin.X, WindowOrigin.Y, FloorZ);
		const FTDFluidGpuFog3DObstacle ExpectedThin = MakeTDFluidGpuFog3DUprightBox(ThinKeys[0].WorldCenter - ThinWindowOffset, ThinKeys[0].HalfExtentsCm, ThinKeys[0].YawDegrees, Inflation);
		TestTrue(TEXT("A thin box is inflated by the module function (center and yaw)"), ThinPacked[0].CenterCmYawCosine == ExpectedThin.CenterCmYawCosine);
		TestTrue(TEXT("A thin box is inflated by the module function (half extents)"), ThinPacked[0].HalfExtentCmYawSine == ExpectedThin.HalfExtentCmYawSine);
		TestTrue(TEXT("A thin box inflates to at least 7.9 on x"), ThinPacked[0].HalfExtentCmYawSine.X >= 7.9f - 1.0e-4f);
		TestTrue(TEXT("A thin box inflates to at least 7.9 on y"), ThinPacked[0].HalfExtentCmYawSine.Y >= 7.9f - 1.0e-4f);
		TestTrue(TEXT("A thin box inflates to at least 15.1 on z"), ThinPacked[0].HalfExtentCmYawSine.Z >= 15.1f - 1.0e-4f);
		TestNearlyEqual(TEXT("Yaw 90 sine"), ThinPacked[0].HalfExtentCmYawSine.W, 1.0f, 1.0e-5f);
		TestNearlyEqual(TEXT("Yaw 90 cosine"), ThinPacked[0].CenterCmYawCosine.W, 0.0f, 1.0e-5f);
	}
	else
	{
		AddError(TEXT("The thin box must be packed"));
	}

	FTDFluidGpuFog3DInflation CustomInflation;
	CustomInflation.BaseHalfThicknessCm = 11.5f;
	CustomInflation.UpwardExtraCm = 3.25f;
	TArray<FTDFluidGpuFog3DObstacle> CustomPacked;
	FTDFog3DLayer::SelectAndPackObstacles(ThinRegistered, WindowOrigin, WindowCenter, FloorZ, CustomInflation, 32, CustomPacked, ThinKeys, Skipped);
	if (CustomPacked.Num() == 1 && ThinKeys.Num() == 1)
	{
		const FVector ThinWindowOffset(WindowOrigin.X, WindowOrigin.Y, FloorZ);
		const FTDFluidGpuFog3DObstacle ExpectedCustom = MakeTDFluidGpuFog3DUprightBox(ThinKeys[0].WorldCenter - ThinWindowOffset, ThinKeys[0].HalfExtentsCm, ThinKeys[0].YawDegrees, CustomInflation);
		TestTrue(TEXT("A custom inflation is passed to the module function (half extents)"), CustomPacked[0].HalfExtentCmYawSine == ExpectedCustom.HalfExtentCmYawSine);
		TestTrue(TEXT("A custom inflation changes the packed half extents"), ThinPacked.Num() == 1 && CustomPacked[0].HalfExtentCmYawSine != ThinPacked[0].HalfExtentCmYawSine);
	}
	else
	{
		AddError(TEXT("The thin box must be packed with a custom inflation"));
	}

	TArray<FTDFluidSdfObstacle> FlippedRegistered;
	FlippedRegistered.Add(Flipped);
	TArray<FTDFluidGpuFog3DObstacle> FlippedPacked;
	FTDFog3DLayer::SelectAndPackObstacles(FlippedRegistered, WindowOrigin, WindowCenter, FloorZ, Inflation, 32, FlippedPacked, ThinKeys, Skipped);
	TestEqual(TEXT("A flipped box is packed"), FlippedPacked.Num(), 1);
	TestEqual(TEXT("A flipped box is not skipped"), Skipped, 0);

	TArray<FTDFluidGpuFog3DObstacle> NoneAllowed;
	FTDFog3DLayer::SelectAndPackObstacles(Registered, WindowOrigin, WindowCenter, FloorZ, Inflation, 0, NoneAllowed, ThinKeys, Skipped);
	TestEqual(TEXT("A limit of zero packs nothing"), NoneAllowed.Num(), 0);

	TArray<FTDFluidGpuFog3DObstacle> OverLimit;
	FTDFog3DLayer::SelectAndPackObstacles(Registered, WindowOrigin, WindowCenter, FloorZ, Inflation, 99, OverLimit, ThinKeys, Skipped);
	TestEqual(TEXT("The module limit caps the configured limit"), OverLimit.Num(), 32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLayerObstacleChangeSignalTest,
	"TDGame.FluidFX.Fog3D.ObstacleChangeSignal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLayerObstacleChangeSignalTest::RunTest(const FString& Parameters)
{
	FTDFog3DObstacleKey First;
	First.WorldCenter = FVector(100.0, 200.0, 50.0);
	First.HalfExtentsCm = FVector(30.0, 40.0, 50.0);
	First.YawDegrees = 15.0;
	FTDFog3DObstacleKey Second;
	Second.WorldCenter = FVector(-300.0, 0.0, 20.0);
	Second.HalfExtentsCm = FVector(10.0, 10.0, 20.0);
	Second.YawDegrees = 0.0;

	const TArray<FTDFog3DObstacleKey> Baseline = { First, Second };
	TestTrue(TEXT("The same list is unchanged"), !FTDFog3DLayer::HaveObstaclesChanged(Baseline, TArray<FTDFog3DObstacleKey>{ First, Second }));
	TestTrue(TEXT("Two empty lists are unchanged"), !FTDFog3DLayer::HaveObstaclesChanged(TArray<FTDFog3DObstacleKey>(), TArray<FTDFog3DObstacleKey>()));

	FTDFog3DObstacleKey Moved = First;
	Moved.WorldCenter.X += 0.01;
	TestTrue(TEXT("A move of 0.01 centimeters changes the list"), FTDFog3DLayer::HaveObstaclesChanged(Baseline, TArray<FTDFog3DObstacleKey>{ Moved, Second }));
	TestTrue(TEXT("A reordered list changes"), FTDFog3DLayer::HaveObstaclesChanged(Baseline, TArray<FTDFog3DObstacleKey>{ Second, First }));
	TestTrue(TEXT("A removed obstacle changes the list"), FTDFog3DLayer::HaveObstaclesChanged(Baseline, TArray<FTDFog3DObstacleKey>{ First }));
	TestTrue(TEXT("An added obstacle changes the list"), FTDFog3DLayer::HaveObstaclesChanged(TArray<FTDFog3DObstacleKey>{ First }, Baseline));

	FTDFog3DObstacleKey Resized = Second;
	Resized.HalfExtentsCm.Z += 0.5;
	TestTrue(TEXT("A size change changes the list"), FTDFog3DLayer::HaveObstaclesChanged(Baseline, TArray<FTDFog3DObstacleKey>{ First, Resized }));
	FTDFog3DObstacleKey Turned = Second;
	Turned.YawDegrees = 0.001;
	TestTrue(TEXT("A yaw change changes the list"), FTDFog3DLayer::HaveObstaclesChanged(Baseline, TArray<FTDFog3DObstacleKey>{ First, Turned }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLayerResetReasonsTest,
	"TDGame.FluidFX.Fog3D.ResetReasons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLayerResetReasonsTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Nothing set does not reset"), !FTDFog3DLayer::ShouldResetState(FTDFog3DResetInputs()));

	{
		FTDFog3DResetInputs Inputs;
		Inputs.bIsFirstStep = true;
		TestTrue(TEXT("The first step resets"), FTDFog3DLayer::ShouldResetState(Inputs));
	}
	{
		FTDFog3DResetInputs Inputs;
		Inputs.bWindowJumpedTooFar = true;
		TestTrue(TEXT("A teleport resets"), FTDFog3DLayer::ShouldResetState(Inputs));
	}
	{
		FTDFog3DResetInputs Inputs;
		Inputs.bZoneReentered = true;
		TestTrue(TEXT("Zone re-entry resets"), FTDFog3DLayer::ShouldResetState(Inputs));
	}
	{
		FTDFog3DResetInputs Inputs;
		Inputs.bConfigurationChanged = true;
		TestTrue(TEXT("A mode or quality change resets"), FTDFog3DLayer::ShouldResetState(Inputs));
	}
	{
		FTDFog3DResetInputs Inputs;
		Inputs.bVolumetricFogReenabled = true;
		TestTrue(TEXT("Volumetric fog turning back on resets"), FTDFog3DLayer::ShouldResetState(Inputs));
	}
	{
		FTDFog3DResetInputs Inputs;
		Inputs.bSawNewRejection = true;
		TestTrue(TEXT("A rejection seen by the layer resets"), FTDFog3DLayer::ShouldResetState(Inputs));
	}

	const FTDFog3DSettings Settings;
	FTDFog3DLayer Layer;
	Layer.Configure(Settings, 0, 2);
	FTDFog3DStepRequest Request;
	Request.Clock = TDFog3DLayerTestPrivate::MakeStepClock(1.0f / 60.0f, 1.0f / 60.0f);
	Request.PlayerWorld = FVector(0.0, 0.0, 0.0);

	TestTrue(TEXT("A new layer resets"), Layer.BuildFrameInput(Request).bResetsState);
	FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, Request);
	TestTrue(TEXT("A steady layer does not reset"), !Layer.BuildFrameInput(Request).bResetsState);

	Layer.RequestReset();
	TestTrue(TEXT("RequestReset resets the next step"), Layer.BuildFrameInput(Request).bResetsState);
	FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, Request);
	TestTrue(TEXT("The reset is consumed by a submitted step"), !Layer.BuildFrameInput(Request).bResetsState);

	Layer.RequestReset(ETDFog3DResetCause::VolumetricFogReenabled);
	TestTrue(TEXT("Volumetric fog re-enable resets the next step"), Layer.BuildFrameInput(Request).bResetsState);
	FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, Request);
	TestTrue(TEXT("The volumetric fog reset is consumed"), !Layer.BuildFrameInput(Request).bResetsState);

	Layer.Configure(Settings, 0, 2);
	TestTrue(TEXT("Configuring the same values does not reset"), !Layer.BuildFrameInput(Request).bResetsState);
	Layer.Configure(Settings, 0, 1);
	TestTrue(TEXT("A mode change resets"), Layer.BuildFrameInput(Request).bResetsState);
	FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, Request);
	Layer.Configure(Settings, 1, 1);
	TestTrue(TEXT("A resolution change resets"), Layer.BuildFrameInput(Request).bResetsState);
	FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, Request);
	Layer.Configure(Settings, 2, 1);
	TestTrue(TEXT("A quality change at the same resolution resets"), Layer.BuildFrameInput(Request).bResetsState);
	FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, Request);
	TestTrue(TEXT("The configuration reset is consumed"), !Layer.BuildFrameInput(Request).bResetsState);

	FTDFog3DStepRequest TeleportRequest = Request;
	TeleportRequest.PlayerWorld = FVector(10000.0, 0.0, 0.0);
	TestTrue(TEXT("A teleport of one hundred meters resets"), Layer.BuildFrameInput(TeleportRequest).bResetsState);
	FTDFog3DStepRequest WalkRequest = Request;
	WalkRequest.PlayerWorld = FVector(1000.0, 0.0, 0.0);
	TestTrue(TEXT("A walk of ten meters does not reset"), !Layer.BuildFrameInput(WalkRequest).bResetsState);

	TestEqual(TEXT("No rejection seen yet"), Layer.GetSeenRejectedStepCount(), 0);
	FTDFog3DLayerTestAccess::AddRenderRejection(Layer);
	TestTrue(TEXT("A new rejection resets the next step"), Layer.BuildFrameInput(Request).bResetsState);
	TestEqual(TEXT("Building does not consume the rejection"), Layer.GetSeenRejectedStepCount(), 0);
	FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, Request);
	TestEqual(TEXT("The submitted step records the rejection it saw"), Layer.GetSeenRejectedStepCount(), 1);
	TestTrue(TEXT("The rejection reset is consumed"), !Layer.BuildFrameInput(Request).bResetsState);

	FTDFog3DStepRequest FirstPoseRequest = Request;
	FirstPoseRequest.Inputs.Add(TDFog3DLayerTestPrivate::MakeSample(0, FVector(100.0, 0.0, 50.0)));
	FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, FirstPoseRequest);
	FTDFog3DStepRequest MovedPoseRequest = Request;
	MovedPoseRequest.Inputs.Add(TDFog3DLayerTestPrivate::MakeSample(0, FVector(300.0, 0.0, 50.0)));
	const FTDFluidGpuFog3DFrameInput Remembered = Layer.BuildFrameInput(MovedPoseRequest);
	if (TestEqual(TEXT("The remembered step has one sphere"), Remembered.Spheres.Num(), 1))
	{
		TestTrue(TEXT("Before forgetting the sphere starts at the previous position"), Remembered.Spheres[0].StartCmRadiusCm.X != Remembered.Spheres[0].EndCmWindStrength.X);
	}
	Layer.ForgetInputHistory();
	const FTDFluidGpuFog3DFrameInput Forgotten = Layer.BuildFrameInput(MovedPoseRequest);
	TestFalse(TEXT("Forgetting input history does not reset the state"), Forgotten.bResetsState);
	if (TestEqual(TEXT("The forgotten step has one sphere"), Forgotten.Spheres.Num(), 1))
	{
		const FTDFluidGpuFog3DSphere& Sphere = Forgotten.Spheres[0];
		TestTrue(TEXT("After forgetting the sphere starts where it ends"), Sphere.StartCmRadiusCm.X == Sphere.EndCmWindStrength.X && Sphere.StartCmRadiusCm.Y == Sphere.EndCmWindStrength.Y && Sphere.StartCmRadiusCm.Z == Sphere.EndCmWindStrength.Z);
		TestTrue(TEXT("After forgetting the sphere has no velocity"), Sphere.VelocityCmPerSecErosionPerSec.X == 0.0f && Sphere.VelocityCmPerSecErosionPerSec.Y == 0.0f && Sphere.VelocityCmPerSecErosionPerSec.Z == 0.0f);
	}

	UTextureRenderTargetVolume* Target = NewObject<UTextureRenderTargetVolume>(GetTransientPackage(), NAME_None, RF_Transient);
	Target->SizeX = 128;
	Target->SizeY = 128;
	Target->SizeZ = TDFluidGpuFog3DLayerCount;
	Target->OverrideFormat = PF_FloatRGBA;
	Target->bSupportsUAV = true;
	TestTrue(TEXT("A half float UAV target of the right size is usable"), FTDFog3DLayer::IsUsableOutputTarget(*Target, 128));
	TestFalse(TEXT("A target of another size is not usable"), FTDFog3DLayer::IsUsableOutputTarget(*Target, 256));
	Target->bSupportsUAV = false;
	TestFalse(TEXT("A target without UAV support is not usable"), FTDFog3DLayer::IsUsableOutputTarget(*Target, 128));
	const uint32 SubmittedBefore = Layer.GetSubmittedStepCount();
	TestFalse(TEXT("SubmitStep refuses a target without UAV support"), Layer.SubmitStep(Request, *Target));
	TestEqual(TEXT("A refused submit records nothing"), Layer.GetSubmittedStepCount(), SubmittedBefore);
	Target->bSupportsUAV = true;
	Target->OverrideFormat = PF_B8G8R8A8;
	TestFalse(TEXT("An eight bit target is not usable"), FTDFog3DLayer::IsUsableOutputTarget(*Target, 128));

	Layer.Release();
	TestTrue(TEXT("Release makes the next step reset"), Layer.BuildFrameInput(Request).bResetsState);
	TestTrue(TEXT("Release forgets the submitted window"), !Layer.HasSubmittedWindow());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLayerFrameInputAssemblyTest,
	"TDGame.FluidFX.Fog3D.FrameInputAssembly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLayerFrameInputAssemblyTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DLayerTestPrivate;

	FTDFog3DSettings Settings;
	Settings.WindRefillPerSec = 0.5f;
	Settings.DisplacementRefillHorizontalPerSec = 0.2f;
	Settings.DisplacementRefillVerticalPerSec = 0.4f;
	Settings.FillRefillPerSec = 0.6f;
	Settings.EdgeBandCells = 6;
	Settings.EdgeBandAbsorptionPerSec = 3.0f;

	FTDFog3DLayer Layer;
	Layer.Configure(Settings, 0, 2);

	FTDFog3DStepRequest FirstRequest;
	FirstRequest.PlayerWorld = FVector(0.0, 0.0, 0.0);
	FirstRequest.FloorZ = 0.0;
	FirstRequest.Clock = MakeStepClock(1.0f / 60.0f, 1.0f / 60.0f);
	FirstRequest.Inputs.Add(MakeSample(0, FVector(100.0, 0.0, 50.0)));
	FirstRequest.RegisteredObstacles.Add(MakeUprightBox(FVector(300.0, 0.0, 50.0), FVector(50.0, 50.0, 50.0), 0.0));

	const FTDFluidGpuFog3DFrameInput First = Layer.BuildFrameInput(FirstRequest);
	TestEqual(TEXT("Resolution comes from the quality tier"), First.Resolution, 128);
	TestNearlyEqual(TEXT("Cell size comes from the settings"), First.CellSizeCm, 15.625f, 1.0e-6f);
	TestNearlyEqual(TEXT("Layer height comes from the settings"), First.LayerHeightCm, 30.0f, 1.0e-6f);
	TestEqual(TEXT("Mode is copied"), First.Mode, 2);
	TestTrue(TEXT("The first window is centered on the player"), First.WindowMinCell == FIntPoint(-64, -64));
	TestTrue(TEXT("The first previous window is the current window"), First.PreviousWindowMinCell == First.WindowMinCell);
	TestTrue(TEXT("The first step resets"), First.bResetsState);
	TestNearlyEqual(TEXT("Delta seconds are copied"), First.DeltaSeconds, 1.0f / 60.0f, 1.0e-7f);
	TestNearlyEqual(TEXT("Wind refill"), First.Tuning.WindRefillPerSec, 0.5f, 1.0e-6f);
	TestNearlyEqual(TEXT("Horizontal displacement refill"), First.Tuning.DisplacementRefillHorizontalPerSec, 0.2f, 1.0e-6f);
	TestNearlyEqual(TEXT("Vertical displacement refill"), First.Tuning.DisplacementRefillVerticalPerSec, 0.4f, 1.0e-6f);
	TestNearlyEqual(TEXT("Fill refill"), First.Tuning.FillRefillPerSec, 0.6f, 1.0e-6f);
	TestEqual(TEXT("Edge band cells"), First.Tuning.EdgeBandCells, 6);
	TestNearlyEqual(TEXT("Edge band absorption"), First.Tuning.EdgeBandAbsorptionPerSec, 3.0f, 1.0e-6f);
	const FTDFluidGpuFog3DTuning DefaultTuning;
	TestEqual(TEXT("Pressure sweeps stay at the module default"), First.Tuning.PressureSweepCount, DefaultTuning.PressureSweepCount);
	TestNearlyEqual(TEXT("Over relaxation stays at the module default"), First.Tuning.PressureOverRelaxation, DefaultTuning.PressureOverRelaxation, 1.0e-6f);
	TestNearlyEqual(TEXT("Horizontal move limit stays at the module default"), First.Tuning.MoveLimitHorizontalCm, DefaultTuning.MoveLimitHorizontalCm, 1.0e-6f);
	TestNearlyEqual(TEXT("Vertical move limit stays at the module default"), First.Tuning.MoveLimitVerticalCm, DefaultTuning.MoveLimitVerticalCm, 1.0e-6f);
	TestEqual(TEXT("One sphere"), First.Spheres.Num(), 1);
	TestEqual(TEXT("One obstacle"), First.Obstacles.Num(), 1);
	TestTrue(TEXT("No output target is attached by the assembly"), First.OutputTarget == nullptr);
	TestFalse(TEXT("The frame input keeps the current wind blend by default"), First.bUsesClampedTimeWindBlend);
	TestTrue(TEXT("Building does not submit"), !Layer.HasSubmittedWindow());

	FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, FirstRequest);
	TestTrue(TEXT("The layer records the submitted window"), Layer.HasSubmittedWindow());
	TestTrue(TEXT("The submitted window is the first window"), Layer.GetSubmittedWindowMinCell() == FIntPoint(-64, -64));
	TestEqual(TEXT("One step counted"), Layer.GetSubmittedStepCount(), 1u);

	FTDFog3DStepRequest SecondRequest = FirstRequest;
	SecondRequest.PlayerWorld = FVector(1280.0, 0.0, 0.0);
	SecondRequest.Clock = MakeStepClock(0.05f, 0.1f);
	SecondRequest.Inputs.Reset();
	SecondRequest.Inputs.Add(MakeSample(0, FVector(200.0, 0.0, 50.0)));
	const FTDFluidGpuFog3DFrameInput Second = Layer.BuildFrameInput(SecondRequest);
	TestTrue(TEXT("The second window follows the player on x only"), Second.WindowMinCell == FIntPoint(16, -64));
	TestTrue(TEXT("The second previous window is the first submitted window"), Second.PreviousWindowMinCell == FIntPoint(-64, -64));
	TestTrue(TEXT("The second step does not reset"), !Second.bResetsState);
	TestTrue(TEXT("The same obstacle list is not a change"), !Second.bObstaclesChanged);
	TestNearlyEqual(TEXT("Second delta seconds"), Second.DeltaSeconds, 0.05f, 1.0e-7f);
	if (Second.Spheres.Num() == 1)
	{
		const FTDFluidGpuFog3DSphere& Sphere = Second.Spheres[0];
		TestNearlyEqual(TEXT("The sphere starts at the last submitted position in the new window"), Sphere.StartCmRadiusCm.X, -150.0f, 1.0e-3f);
		TestNearlyEqual(TEXT("Start y in the new window"), Sphere.StartCmRadiusCm.Y, 1000.0f, 1.0e-3f);
		TestNearlyEqual(TEXT("End x in the new window"), Sphere.EndCmWindStrength.X, -50.0f, 1.0e-3f);
		TestNearlyEqual(TEXT("Velocity uses the real elapsed time"), Sphere.VelocityCmPerSecErosionPerSec.X, 1000.0f, 0.01f);
	}
	else
	{
		AddError(TEXT("The second frame must keep the sphere"));
	}

	FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, SecondRequest);
	TestTrue(TEXT("The submitted window moved"), Layer.GetSubmittedWindowMinCell() == FIntPoint(16, -64));

	FTDFog3DStepRequest ChangedRequest = SecondRequest;
	ChangedRequest.RegisteredObstacles.Reset();
	const FTDFluidGpuFog3DFrameInput Changed = Layer.BuildFrameInput(ChangedRequest);
	TestTrue(TEXT("Removing the obstacle signals a change"), Changed.bObstaclesChanged);
	TestEqual(TEXT("No obstacles remain"), Changed.Obstacles.Num(), 0);

	FTDFog3DStepRequest ManyRequest = SecondRequest;
	ManyRequest.Inputs.Reset();
	for (int32 InputIndex = 0; InputIndex < 17; ++InputIndex)
	{
		ManyRequest.Inputs.Add(MakeSample(InputIndex, FVector(200.0, 0.0, 50.0)));
	}
	ManyRequest.RegisteredObstacles.Add(UTDFluidObstacleComponent::MakeOrientedBoxObstacle(FVector(0.0, 500.0, 50.0), FVector(50.0), FRotator(0.0, 0.0, 90.0)));
	FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, ManyRequest);
	TestEqual(TEXT("Seventeen inputs report one dropped sphere"), Layer.GetLastDroppedSphereCount(), 1);
	TestEqual(TEXT("A sideways box reports one skipped obstacle"), Layer.GetLastSkippedObstacleCount(), 1);

	Layer.Configure(Settings, 0, 1);
	TestEqual(TEXT("The mode is copied after configuring"), Layer.BuildFrameInput(SecondRequest).Mode, 1);
	Layer.Configure(Settings, 1, 2);
	FTDFog3DStepRequest ResizedRequest = SecondRequest;
	ResizedRequest.PlayerWorld = FVector(154.5 * 15.625, 64.5 * 15.625, 0.0);
	const FTDFluidGpuFog3DFrameInput Resized = Layer.BuildFrameInput(ResizedRequest);
	TestEqual(TEXT("The resolution is copied after configuring"), Resized.Resolution, 256);
	TestTrue(TEXT("A new resolution centers a fresh window instead of following the old one"), Resized.WindowMinCell == FIntPoint(32, -64));

	Layer.Release();
	const FTDFluidGpuFog3DFrameInput AfterRelease = Layer.BuildFrameInput(SecondRequest);
	TestTrue(TEXT("After release the step resets"), AfterRelease.bResetsState);
	TestTrue(TEXT("After release the previous window is the current window"), AfterRelease.PreviousWindowMinCell == AfterRelease.WindowMinCell);
	TestEqual(TEXT("Release resets the step count"), Layer.GetSubmittedStepCount(), 0u);

	FTDFog3DStepRequest GainedRequest = SecondRequest;
	GainedRequest.Inputs[0].WindGain = 1.25f;
	FTDFog3DLayer CurrentBlendLayer;
	CurrentBlendLayer.Configure(Settings, 0, 2);
	FTDFog3DLayerTestAccess::CommitWithoutRendering(CurrentBlendLayer, FirstRequest);
	const FTDFluidGpuFog3DFrameInput CurrentBlend = CurrentBlendLayer.BuildFrameInput(GainedRequest);
	FTDFog3DSettings ClampedTimeSettings = Settings;
	ClampedTimeSettings.bUsesClampedTimeWindBlend = true;
	FTDFog3DLayer ClampedTimeLayer;
	ClampedTimeLayer.Configure(ClampedTimeSettings, 0, 2);
	FTDFog3DLayerTestAccess::CommitWithoutRendering(ClampedTimeLayer, FirstRequest);
	const FTDFluidGpuFog3DFrameInput ClampedTime = ClampedTimeLayer.BuildFrameInput(GainedRequest);
	TestFalse(TEXT("The switch stays off in the frame input when the settings keep it off"), CurrentBlend.bUsesClampedTimeWindBlend);
	TestTrue(TEXT("The clamped time wind blend switch reaches the frame input"), ClampedTime.bUsesClampedTimeWindBlend);
	if (CurrentBlend.Spheres.Num() == 1 && ClampedTime.Spheres.Num() == 1)
	{
		TestNearlyEqual(TEXT("With the switch off the layer ignores the input wind gain"), CurrentBlend.Spheres[0].VelocityCmPerSecErosionPerSec.X, 1000.0f, 0.01f);
		TestNearlyEqual(TEXT("With the switch on the layer packs the gained velocity"), ClampedTime.Spheres[0].VelocityCmPerSecErosionPerSec.X, 1250.0f, 0.01f);
	}
	else
	{
		AddError(TEXT("Gained frames must keep the sphere"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLayerMaterialWindowParametersTest,
	"TDGame.FluidFX.Fog3D.MaterialWindowParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLayerMaterialWindowParametersTest::RunTest(const FString& Parameters)
{
	const double CellSizeCm = 15.625;

	const FVector4f Simple = FTDFog3DLayer::ComputeMaterialWindow(FIntPoint(16, -32), CellSizeCm, FVector(100.0, 200.0, 150.0), 0.0, 256);
	TestNearlyEqual(TEXT("Simple window x"), Simple.X, 16.0f * 15.625f - 100.0f, 1.0e-3f);
	TestNearlyEqual(TEXT("Simple window y"), Simple.Y, -32.0f * 15.625f - 200.0f, 1.0e-3f);
	TestNearlyEqual(TEXT("Simple window z is the floor relative to the zone reference"), Simple.Z, -150.0f, 1.0e-3f);
	TestNearlyEqual(TEXT("Simple window cell count"), Simple.W, 256.0f, 1.0e-6f);

	const FVector LargeReference(1.0e7, -1.0e7, 500.0);
	const FVector4f Aligned = FTDFog3DLayer::ComputeMaterialWindow(FIntPoint(640000 + 48, -640000 - 32), CellSizeCm, LargeReference, 0.0, 128);
	TestNearlyEqual(TEXT("Aligned large window x"), Aligned.X, 750.0f, 0.01f);
	TestNearlyEqual(TEXT("Aligned large window y"), Aligned.Y, -500.0f, 0.01f);
	TestNearlyEqual(TEXT("Aligned large window z"), Aligned.Z, -500.0f, 0.01f);
	TestNearlyEqual(TEXT("Aligned large window cell count"), Aligned.W, 128.0f, 1.0e-6f);

	const FVector OffsetReference(1.0e7 + 3.37, -1.0e7 - 1.21, 500.5);
	const FIntPoint OffsetWindow(640000 + 16, -640000 + 64);
	const FVector4f Offset = FTDFog3DLayer::ComputeMaterialWindow(OffsetWindow, CellSizeCm, OffsetReference, 12.25, 256);
	const double ExpectedX = static_cast<double>(OffsetWindow.X) * CellSizeCm - OffsetReference.X;
	const double ExpectedY = static_cast<double>(OffsetWindow.Y) * CellSizeCm - OffsetReference.Y;
	const double ExpectedZ = 12.25 - OffsetReference.Z;
	TestTrue(TEXT("Offset large window x within 0.01 centimeters"), FMath::Abs(static_cast<double>(Offset.X) - ExpectedX) < 0.01);
	TestTrue(TEXT("Offset large window y within 0.01 centimeters"), FMath::Abs(static_cast<double>(Offset.Y) - ExpectedY) < 0.01);
	TestTrue(TEXT("Offset large window z within 0.01 centimeters"), FMath::Abs(static_cast<double>(Offset.Z) - ExpectedZ) < 0.01);

	const float NaiveX = static_cast<float>(OffsetWindow.X * CellSizeCm) - static_cast<float>(OffsetReference.X);
	TestTrue(TEXT("Subtracting after narrowing to 32 bits would be off by more than 0.1 centimeters"), FMath::Abs(static_cast<double>(NaiveX) - ExpectedX) > 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DLayerStepPathTest,
	"TDGame.FluidFX.Fog3D.LayerStepPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DLayerStepPathTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DLayerTestPrivate;

	const FTDFog3DSettings DefaultSettings;
	TestEqual(TEXT("The default follow margin is twelve cells"), DefaultSettings.WindowFollowMarginCells, 12);
	TestEqual(TEXT("The default speed limit is thirty meters per second"), DefaultSettings.MaximumInputSpeedCmPerSec, 3000.0f);

	const auto ExpectFollowedWindow = [this](const FTDFog3DSettings& Data, int32 CellX, int32 CellY, const FIntPoint& Expected)
	{
		FTDFog3DLayer Layer;
		Layer.Configure(Data, 0, 2);
		FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, MakePathRequest(MakeCellCenterWorld(0, 0), nullptr));
		TestTrue(TEXT("The first window is centered on the origin cell"), Layer.GetSubmittedWindowMinCell() == FIntPoint(-64, -64));
		const FTDFluidGpuFog3DFrameInput Built = Layer.BuildFrameInput(MakePathRequest(MakeCellCenterWorld(CellX, CellY), nullptr));
		TestTrue(FString::Printf(TEXT("Margin %d with the player at cell (%d, %d) puts the window at %s (got %s)"), Data.WindowFollowMarginCells, CellX, CellY, *DescribeCell(Expected), *DescribeCell(Built.WindowMinCell)), Built.WindowMinCell == Expected);
		TestFalse(TEXT("Following the player does not reset"), Built.bResetsState);
	};
	ExpectFollowedWindow(DefaultSettings, 12, 0, FIntPoint(-64, -64));
	ExpectFollowedWindow(DefaultSettings, 13, 0, FIntPoint(-48, -64));
	ExpectFollowedWindow(DefaultSettings, 0, 12, FIntPoint(-64, -64));
	ExpectFollowedWindow(DefaultSettings, 0, 13, FIntPoint(-64, -48));
	ExpectFollowedWindow(DefaultSettings, -12, 0, FIntPoint(-64, -64));
	ExpectFollowedWindow(DefaultSettings, -13, 0, FIntPoint(-80, -64));
	ExpectFollowedWindow(DefaultSettings, 0, -12, FIntPoint(-64, -64));
	ExpectFollowedWindow(DefaultSettings, 0, -13, FIntPoint(-64, -80));
	ExpectFollowedWindow(DefaultSettings, 12, -12, FIntPoint(-64, -64));
	FTDFog3DSettings WideMarginSettings;
	WideMarginSettings.WindowFollowMarginCells = 20;
	ExpectFollowedWindow(WideMarginSettings, 13, 0, FIntPoint(-64, -64));
	ExpectFollowedWindow(WideMarginSettings, 20, 0, FIntPoint(-64, -64));
	ExpectFollowedWindow(WideMarginSettings, 21, 0, FIntPoint(-48, -64));
	ExpectFollowedWindow(WideMarginSettings, 0, -21, FIntPoint(-64, -80));

	const FVector RestingSample(100.0, 0.0, 50.0);
	const FVector WalkedSample(110.0, 0.0, 50.0);
	const FVector WindowOriginWorld(-1000.0, -1000.0, 0.0);
	const auto MakeRememberingLayer = [&DefaultSettings, &RestingSample](FTDFog3DLayer& Layer)
	{
		Layer.Configure(DefaultSettings, 0, 2);
		FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, MakePathRequest(MakeCellCenterWorld(0, 0), &RestingSample));
	};

	{
		FTDFog3DLayer Layer;
		MakeRememberingLayer(Layer);
		const FTDFluidGpuFog3DFrameInput Walked = Layer.BuildFrameInput(MakePathRequest(MakeCellCenterWorld(0, 0), &WalkedSample));
		TestFalse(TEXT("A steady layer does not reset"), Walked.bResetsState);
		if (TestEqual(TEXT("The walked step has one sphere"), Walked.Spheres.Num(), 1))
		{
			const FTDFluidGpuFog3DSphere& Sphere = Walked.Spheres[0];
			TestNearlyEqual(TEXT("A steady layer starts the sphere at the last submitted position"), Sphere.StartCmRadiusCm.X, static_cast<float>(RestingSample.X - WindowOriginWorld.X), 1.0e-3f);
			TestNearlyEqual(TEXT("A steady layer ends the sphere at the new position"), Sphere.EndCmWindStrength.X, static_cast<float>(WalkedSample.X - WindowOriginWorld.X), 1.0e-3f);
			TestNearlyEqual(TEXT("A steady layer gives the sphere the walking speed"), Sphere.VelocityCmPerSecErosionPerSec.X, 600.0f, 0.01f);
			TestFalse(TEXT("A walking sphere is not at rest"), IsSphereAtRest(Sphere));
		}
	}

	const auto ExpectSphereAtRestAfterReset = [this, &MakeRememberingLayer](const TCHAR* Name, const TFunction<FTDFog3DStepRequest(FTDFog3DLayer&)>& TriggerReset)
	{
		FTDFog3DLayer Layer;
		MakeRememberingLayer(Layer);
		const FTDFog3DStepRequest Request = TriggerReset(Layer);
		const FTDFluidGpuFog3DFrameInput Built = Layer.BuildFrameInput(Request);
		TestTrue(FString::Printf(TEXT("%s resets the step"), Name), Built.bResetsState);
		if (TestEqual(FString::Printf(TEXT("%s keeps the sphere"), Name), Built.Spheres.Num(), 1))
		{
			TestTrue(FString::Printf(TEXT("%s starts the sphere where it ends with no velocity"), Name), IsSphereAtRest(Built.Spheres[0]));
		}
	};
	ExpectSphereAtRestAfterReset(TEXT("A zone re-entry request"), [&WalkedSample](FTDFog3DLayer& Layer)
	{
		Layer.RequestReset();
		return MakePathRequest(MakeCellCenterWorld(0, 0), &WalkedSample);
	});
	ExpectSphereAtRestAfterReset(TEXT("A volumetric fog re-enable request"), [&WalkedSample](FTDFog3DLayer& Layer)
	{
		Layer.RequestReset(ETDFog3DResetCause::VolumetricFogReenabled);
		return MakePathRequest(MakeCellCenterWorld(0, 0), &WalkedSample);
	});
	ExpectSphereAtRestAfterReset(TEXT("A rejection seen by the layer"), [&WalkedSample](FTDFog3DLayer& Layer)
	{
		FTDFog3DLayerTestAccess::AddRenderRejection(Layer);
		return MakePathRequest(MakeCellCenterWorld(0, 0), &WalkedSample);
	});
	ExpectSphereAtRestAfterReset(TEXT("A configuration change"), [&WalkedSample](FTDFog3DLayer& Layer)
	{
		Layer.Configure(FTDFog3DSettings(), 0, 1);
		return MakePathRequest(MakeCellCenterWorld(0, 0), &WalkedSample);
	});
	ExpectSphereAtRestAfterReset(TEXT("A teleport of one hundred meters"), [](FTDFog3DLayer&)
	{
		const FVector FarSample(10010.0, 0.0, 50.0);
		return MakePathRequest(MakeCellCenterWorld(640, 0), &FarSample);
	});

	for (const float SpeedLimit : { 3000.0f, 1234.0f })
	{
		FTDFog3DSettings FastSettings;
		FastSettings.MaximumInputSpeedCmPerSec = SpeedLimit;
		FTDFog3DLayer Layer;
		Layer.Configure(FastSettings, 0, 2);
		FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, MakePathRequest(MakeCellCenterWorld(0, 0), &RestingSample));
		const FVector FastSample(600.0, 0.0, 50.0);
		FTDFog3DStepRequest FastRequest = MakePathRequest(MakeCellCenterWorld(0, 0), &FastSample);
		FastRequest.Clock = MakeStepClock(0.05f, 0.1f);
		const FTDFluidGpuFog3DFrameInput Built = Layer.BuildFrameInput(FastRequest);
		if (TestEqual(TEXT("The fast step has one sphere"), Built.Spheres.Num(), 1))
		{
			const FTDFluidGpuFog3DSphere& Sphere = Built.Spheres[0];
			TestNearlyEqual(*FString::Printf(TEXT("Fifty meters per second is cut to the data limit %.0f"), SpeedLimit), Sphere.VelocityCmPerSecErosionPerSec.X, SpeedLimit, 0.05f);
			TestNearlyEqual(TEXT("The cut keeps the direction"), Sphere.VelocityCmPerSecErosionPerSec.Y, 0.0f, 1.0e-3f);
			TestNearlyEqual(TEXT("The swept length is not cut"), Sphere.EndCmWindStrength.X - Sphere.StartCmRadiusCm.X, 500.0f, 1.0e-3f);
		}
	}

	{
		FTDFog3DLayer Layer;
		Layer.Configure(DefaultSettings, 0, 2);
		FTDFog3DLayerTestAccess::CommitWithoutRendering(Layer, MakePathRequest(MakeCellCenterWorld(0, 0), nullptr));

		TArray<FTDFluidSdfObstacle> Registered;
		for (int32 NearIndex = 0; NearIndex < 31; ++NearIndex)
		{
			Registered.Add(MakeUprightBox(FVector(-150.0 + 10.0 * NearIndex, 0.0, 50.0), FVector(20.0, 20.0, 50.0), 0.0));
		}
		const TArray<FVector2D> Candidates = { FVector2D(-3000.0, -2500.0), FVector2D(-3000.0, 1000.0), FVector2D(2250.0, 2500.0) };
		for (const FVector2D& Candidate : Candidates)
		{
			Registered.Add(MakeUprightBox(FVector(Candidate.X, Candidate.Y, 50.0), FVector(50.0, 50.0, 50.0), 0.0));
		}

		const FVector PlayerWorld = MakeCellCenterWorld(12, 12);
		FTDFog3DStepRequest Request = MakePathRequest(PlayerWorld, nullptr);
		Request.RegisteredObstacles = Registered;
		const FTDFluidGpuFog3DFrameInput Built = Layer.BuildFrameInput(Request);
		TestTrue(TEXT("The window did not move for a player twelve cells from the center"), Built.WindowMinCell == FIntPoint(-64, -64));
		TestEqual(TEXT("Thirty four boxes are cut to the data limit"), Built.Obstacles.Num(), DefaultSettings.MaximumObstacleCount);

		FTDFluidGpuFog3DInflation Inflation;
		Inflation.BaseHalfThicknessCm = DefaultSettings.ObstacleBaseHalfThicknessCm;
		Inflation.UpwardExtraCm = DefaultSettings.ObstacleUpwardExtraCm;
		const FVector2D WindowOrigin(WindowOriginWorld.X, WindowOriginWorld.Y);
		const auto SelectFor = [&](const FVector2D& DistanceReference)
		{
			TArray<FTDFluidGpuFog3DObstacle> Packed;
			TArray<FTDFog3DObstacleKey> Keys;
			int32 Skipped = 0;
			FTDFog3DLayer::SelectAndPackObstacles(Registered, WindowOrigin, DistanceReference, 0.0, Inflation, DefaultSettings.MaximumObstacleCount, Packed, Keys, Skipped);
			return Packed;
		};
		const TArray<FTDFluidGpuFog3DObstacle> ByWindowCenter = SelectFor(FVector2D(0.0, 0.0));
		const TArray<FTDFluidGpuFog3DObstacle> ByPlayer = SelectFor(FVector2D(PlayerWorld.X, PlayerWorld.Y));
		const TArray<FTDFluidGpuFog3DObstacle> ByWindowOrigin = SelectFor(WindowOrigin);
		TestFalse(TEXT("Choosing by the player would pick other boxes than choosing by the window center"), AreObstacleListsEqual(ByWindowCenter, ByPlayer));
		TestFalse(TEXT("Choosing by the window origin would pick other boxes than choosing by the window center"), AreObstacleListsEqual(ByWindowCenter, ByWindowOrigin));
		TestFalse(TEXT("The player and the window origin choose different boxes"), AreObstacleListsEqual(ByPlayer, ByWindowOrigin));
		TestTrue(TEXT("The real path picks boxes by distance from the window center"), AreObstacleListsEqual(Built.Obstacles, ByWindowCenter));
	}

	{
		const auto MakeGuardTarget = []()
		{
			UTextureRenderTargetVolume* Target = NewObject<UTextureRenderTargetVolume>(GetTransientPackage(), NAME_None, RF_Transient);
			Target->SizeX = 128;
			Target->SizeY = 128;
			Target->SizeZ = TDFluidGpuFog3DLayerCount;
			Target->OverrideFormat = PF_FloatRGBA;
			Target->bSupportsUAV = true;
			return Target;
		};
		struct FGuardCase
		{
			const TCHAR* Name;
			int32 Mode;
			TFunction<void(FTDFog3DStepRequest&, UTextureRenderTargetVolume&)> Change;
		};
		const TArray<FGuardCase> GuardCases = {
			{ TEXT("A target that passes every guard but has no render resource"), 2, [](FTDFog3DStepRequest&, UTextureRenderTargetVolume&) {} },
			{ TEXT("Mode 0 refuses"), 0, [](FTDFog3DStepRequest&, UTextureRenderTargetVolume&) {} },
			{ TEXT("A clock that says not now refuses"), 2, [](FTDFog3DStepRequest& Request, UTextureRenderTargetVolume&) { Request.Clock.bStepsNow = false; } },
			{ TEXT("A zero step delta refuses"), 2, [](FTDFog3DStepRequest& Request, UTextureRenderTargetVolume&) { Request.Clock.StepDeltaSeconds = 0.0f; } },
			{ TEXT("A not a number step delta refuses"), 2, [](FTDFog3DStepRequest& Request, UTextureRenderTargetVolume&) { Request.Clock.StepDeltaSeconds = std::numeric_limits<float>::quiet_NaN(); } },
			{ TEXT("A not a number player refuses"), 2, [](FTDFog3DStepRequest& Request, UTextureRenderTargetVolume&) { Request.PlayerWorld.X = std::numeric_limits<double>::quiet_NaN(); } },
			{ TEXT("A not a number floor refuses"), 2, [](FTDFog3DStepRequest& Request, UTextureRenderTargetVolume&) { Request.FloorZ = std::numeric_limits<double>::quiet_NaN(); } },
			{ TEXT("A target of another width refuses"), 2, [](FTDFog3DStepRequest&, UTextureRenderTargetVolume& Target) { Target.SizeX = 64; } },
			{ TEXT("A target of another height refuses"), 2, [](FTDFog3DStepRequest&, UTextureRenderTargetVolume& Target) { Target.SizeY = 64; } },
			{ TEXT("A target of another layer count refuses"), 2, [](FTDFog3DStepRequest&, UTextureRenderTargetVolume& Target) { Target.SizeZ = TDFluidGpuFog3DLayerCount + 1; } },
			{ TEXT("A target of another format refuses"), 2, [](FTDFog3DStepRequest&, UTextureRenderTargetVolume& Target) { Target.OverrideFormat = PF_B8G8R8A8; } },
			{ TEXT("A target without UAV support refuses"), 2, [](FTDFog3DStepRequest&, UTextureRenderTargetVolume& Target) { Target.bSupportsUAV = false; } },
		};
		for (const FGuardCase& Case : GuardCases)
		{
			FTDFog3DLayer Layer;
			Layer.Configure(DefaultSettings, 0, Case.Mode);
			Layer.RequestReset();
			UTextureRenderTargetVolume* Target = MakeGuardTarget();
			FTDFog3DStepRequest Request = MakePathRequest(MakeCellCenterWorld(0, 0), &RestingSample);
			Case.Change(Request, *Target);
			TestFalse(Case.Name, Layer.SubmitStep(Request, *Target));
			TestEqual(FString::Printf(TEXT("%s: nothing is counted"), Case.Name), Layer.GetSubmittedStepCount(), 0u);
			TestFalse(FString::Printf(TEXT("%s: no window is recorded"), Case.Name), Layer.HasSubmittedWindow());
			TestTrue(FString::Printf(TEXT("%s: the pending reset is not consumed"), Case.Name), FTDFog3DLayerTestAccess::IsZoneReentryPending(Layer));
		}
	}
	return true;
}

#endif
