#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"
#include "FluidFX/TDFluidTerrainCapture.h"
#include "FluidFX/TDFluidVolumeDriverComponent.h"
#include "FluidFX/TDFog3DGround.h"
#include "FluidFX/TDFog3DLayer.h"
#include "FluidFX/TDFog3DZoneActor.h"
#include "FluidFX/Tests/TDFog3DZoneActorGroundTestAccess.h"
#include "GameFramework/Pawn.h"
#include "LandscapeComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace TDFog3DGroundTestPrivate
{
	constexpr double GroundCellSizeCm = 15.625;

	struct FTDFog3DGroundTestWorldScope
	{
		FTDFog3DGroundTestWorldScope()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFog3DGroundTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFog3DGroundTestWorldScope()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		UWorld* World = nullptr;
	};

	struct FTDFog3DGroundBadSettingsCase
	{
		FString Name;
		TFunction<void(FTDFog3DSettings&)> Mutate;
	};

	TArray<FTDFog3DGroundBadSettingsCase> MakeBadGroundSettingsCases()
	{
		TArray<FTDFog3DGroundBadSettingsCase> Cases;
		Cases.Add({ TEXT("wall step zero"), [](FTDFog3DSettings& S) { S.GroundWallStepCm = 0.0f; } });
		Cases.Add({ TEXT("wall step negative"), [](FTDFog3DSettings& S) { S.GroundWallStepCm = -30.0f; } });
		Cases.Add({ TEXT("wall step not a number"), [](FTDFog3DSettings& S) { S.GroundWallStepCm = std::numeric_limits<float>::quiet_NaN(); } });
		Cases.Add({ TEXT("border 16 cells"), [](FTDFog3DSettings& S) { S.GroundCaptureBorderCells = 16; } });
		Cases.Add({ TEXT("border negative"), [](FTDFog3DSettings& S) { S.GroundCaptureBorderCells = -64; } });
		Cases.Add({ TEXT("capture height above negative"), [](FTDFog3DSettings& S) { S.GroundCaptureHeightAboveZoneCm = -1.0f; } });
		Cases.Add({ TEXT("capture height above infinite"), [](FTDFog3DSettings& S) { S.GroundCaptureHeightAboveZoneCm = std::numeric_limits<float>::infinity(); } });
		Cases.Add({ TEXT("latency zero"), [](FTDFog3DSettings& S) { S.GroundCaptureLatencyFrames = 0; } });
		Cases.Add({ TEXT("latency five"), [](FTDFog3DSettings& S) { S.GroundCaptureLatencyFrames = 5; } });
		Cases.Add({ TEXT("missing height not a number"), [](FTDFog3DSettings& S) { S.GroundMissingHeightCm = std::numeric_limits<float>::quiet_NaN(); } });
		Cases.Add({ TEXT("missing height infinite"), [](FTDFog3DSettings& S) { S.GroundMissingHeightCm = -std::numeric_limits<float>::infinity(); } });
		Cases.Add({ TEXT("missing below floor zero"), [](FTDFog3DSettings& S) { S.GroundMissingBelowFloorCm = 0.0f; } });
		Cases.Add({ TEXT("missing below floor negative"), [](FTDFog3DSettings& S) { S.GroundMissingBelowFloorCm = -1.0f; } });
		Cases.Add({ TEXT("missing below floor infinite"), [](FTDFog3DSettings& S) { S.GroundMissingBelowFloorCm = std::numeric_limits<float>::infinity(); } });
		Cases.Add({ TEXT("missing below floor not a number"), [](FTDFog3DSettings& S) { S.GroundMissingBelowFloorCm = std::numeric_limits<float>::quiet_NaN(); } });
		Cases.Add({ TEXT("ground following on without a ground material"), [](FTDFog3DSettings& S) { S.bFollowsGround = true; } });
		return Cases;
	}

	FTDFog3DGroundBadSettingsCase MakeAcceptedCase(const TCHAR* Name, TFunction<void(FTDFog3DSettings&)> Mutate)
	{
		return { Name, MoveTemp(Mutate) };
	}

	FTextureRenderTargetResource* MakeSentinelResource(UPTRINT Address)
	{
		return reinterpret_cast<FTextureRenderTargetResource*>(Address);
	}

	FTDFog3DClockDecision MakeStepClock()
	{
		FTDFog3DClockDecision Clock;
		Clock.bStepsNow = true;
		Clock.StepDeltaSeconds = 1.0f / 60.0f;
		Clock.ElapsedSeconds = 1.0f / 60.0f;
		return Clock;
	}

	bool IsNear(const FVector4f& Actual, const FVector4f& Expected, float Tolerance)
	{
		return FMath::Abs(Actual.X - Expected.X) <= Tolerance
			&& FMath::Abs(Actual.Y - Expected.Y) <= Tolerance
			&& FMath::Abs(Actual.Z - Expected.Z) <= Tolerance
			&& FMath::Abs(Actual.W - Expected.W) <= Tolerance;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DGroundSettingsValidationTest,
	"TDGame.FluidFX.Fog3D.GroundSettingsValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DGroundSettingsValidationTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DGroundTestPrivate;

	const FTDFog3DSettings Defaults;
	TestFalse(TEXT("Ground following is off by default"), Defaults.bFollowsGround);
	TestEqual(TEXT("Default wall step"), Defaults.GroundWallStepCm, 30.0f);
	TestEqual(TEXT("Default capture border"), Defaults.GroundCaptureBorderCells, 64);
	TestEqual(TEXT("Default capture height above the zone"), Defaults.GroundCaptureHeightAboveZoneCm, 500.0f);
	TestEqual(TEXT("Default capture latency"), Defaults.GroundCaptureLatencyFrames, 2);
	TestEqual(TEXT("Default missing height is the zone floor"), Defaults.GroundMissingHeightCm, 0.0f);
	TestEqual(TEXT("Default missing below floor margin"), Defaults.GroundMissingBelowFloorCm, 5000.0f);
	TestTrue(TEXT("Default ground material is empty until the asset exists"), Defaults.GroundFollowVolumeMaterial.IsNull());

	TArray<FString> DefaultProblems;
	CollectTDFog3DSettingsProblems(Defaults, DefaultProblems);
	TestEqual(*FString::Printf(TEXT("Header defaults have no problems (%s)"), *FString::Join(DefaultProblems, TEXT("; "))), DefaultProblems.Num(), 0);

	for (const FTDFog3DGroundBadSettingsCase& BadCase : MakeBadGroundSettingsCases())
	{
		FTDFog3DSettings Mutated;
		BadCase.Mutate(Mutated);
		TArray<FString> Problems;
		CollectTDFog3DSettingsProblems(Mutated, Problems);
		TestTrue(*FString::Printf(TEXT("Rejects: %s"), *BadCase.Name), Problems.Num() >= 1);
	}

	const TArray<FTDFog3DGroundBadSettingsCase> AcceptedCases = {
		MakeAcceptedCase(TEXT("border 17 cells"), [](FTDFog3DSettings& S) { S.GroundCaptureBorderCells = 17; }),
		MakeAcceptedCase(TEXT("latency one"), [](FTDFog3DSettings& S) { S.GroundCaptureLatencyFrames = 1; }),
		MakeAcceptedCase(TEXT("latency four"), [](FTDFog3DSettings& S) { S.GroundCaptureLatencyFrames = 4; }),
		MakeAcceptedCase(TEXT("capture height above zero"), [](FTDFog3DSettings& S) { S.GroundCaptureHeightAboveZoneCm = 0.0f; }),
		MakeAcceptedCase(TEXT("missing height below the floor"), [](FTDFog3DSettings& S) { S.GroundMissingHeightCm = -250.0f; }),
		MakeAcceptedCase(TEXT("ground following on with a material path"), [](FTDFog3DSettings& S)
		{
			S.bFollowsGround = true;
			S.GroundFollowVolumeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/FluidFX/Materials/M_TD_Fog3DVolumeGround.M_TD_Fog3DVolumeGround")));
		}),
	};
	for (const FTDFog3DGroundBadSettingsCase& AcceptedCase : AcceptedCases)
	{
		FTDFog3DSettings Mutated;
		AcceptedCase.Mutate(Mutated);
		TArray<FString> Problems;
		CollectTDFog3DSettingsProblems(Mutated, Problems);
		TestEqual(*FString::Printf(TEXT("Accepts: %s (%s)"), *AcceptedCase.Name, *FString::Join(Problems, TEXT("; "))), Problems.Num(), 0);
	}

	FTDFog3DSettings Scribbled;
	Scribbled.bFollowsGround = true;
	Scribbled.GroundWallStepCm = 11.0f;
	Scribbled.GroundCaptureBorderCells = 99;
	Scribbled.GroundCaptureHeightAboveZoneCm = 7.0f;
	Scribbled.GroundCaptureLatencyFrames = 3;
	Scribbled.GroundMissingHeightCm = -9.0f;
	Scribbled.GroundMissingBelowFloorCm = 13.0f;
	Scribbled.GroundFollowVolumeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Scribbled/M_Scribbled.M_Scribbled")));
	UTDFluidPresetTuningSubsystem::ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFog3DSettings::StaticStruct(), &Scribbled);
	TestEqual(TEXT("bFollowsGround comes from the table"), Scribbled.bFollowsGround, Defaults.bFollowsGround);
	TestEqual(TEXT("GroundWallStepCm comes from the table"), Scribbled.GroundWallStepCm, Defaults.GroundWallStepCm);
	TestEqual(TEXT("GroundCaptureBorderCells comes from the table"), Scribbled.GroundCaptureBorderCells, Defaults.GroundCaptureBorderCells);
	TestEqual(TEXT("GroundCaptureHeightAboveZoneCm comes from the table"), Scribbled.GroundCaptureHeightAboveZoneCm, Defaults.GroundCaptureHeightAboveZoneCm);
	TestEqual(TEXT("GroundCaptureLatencyFrames comes from the table"), Scribbled.GroundCaptureLatencyFrames, Defaults.GroundCaptureLatencyFrames);
	TestEqual(TEXT("GroundMissingHeightCm comes from the table"), Scribbled.GroundMissingHeightCm, Defaults.GroundMissingHeightCm);
	TestEqual(TEXT("GroundMissingBelowFloorCm comes from the table"), Scribbled.GroundMissingBelowFloorCm, Defaults.GroundMissingBelowFloorCm);
	TestTrue(TEXT("GroundFollowVolumeMaterial comes from the table as an empty path"), Scribbled.GroundFollowVolumeMaterial.IsNull());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DGroundCaptureRectTest,
	"TDGame.FluidFX.Fog3D.GroundCaptureRect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DGroundCaptureRectTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DGroundTestPrivate;

	TestEqual(TEXT("Capture size is N plus twice the border"), FTDFog3DGroundLayout::ComputeCaptureSize(128, 64), 256);
	TestEqual(TEXT("Capture size for 256 cells"), FTDFog3DGroundLayout::ComputeCaptureSize(256, 64), 384);
	TestEqual(TEXT("No window gives no capture"), FTDFog3DGroundLayout::ComputeCaptureSize(0, 64), 0);
	TestEqual(TEXT("A negative border gives no capture"), FTDFog3DGroundLayout::ComputeCaptureSize(128, -1), 0);

	const FIntPoint SnappedWindow = FTDFog3DLayer::SnapWindowMinCell(FIntPoint(0, 0), 128);
	const FIntPoint SnappedCenter = FTDFog3DGroundLayout::ComputeCaptureCenterCell(SnappedWindow, 128);
	TestTrue(TEXT("Center cell is the window minimum plus N / 2"), SnappedCenter == SnappedWindow + FIntPoint(64, 64));
	TestTrue(TEXT("Center cell of a snapped window is a multiple of 16"), SnappedCenter.X % 16 == 0 && SnappedCenter.Y % 16 == 0);

	const double FloorZ = 1250.25;
	const double ZoneHalfHeightCm = 150.0;
	const FTDFog3DGroundCapturePlacement Placement = FTDFog3DGroundLayout::ComputeCapturePlacement(FIntPoint(-64, 32), 128, 64, GroundCellSizeCm, FloorZ, ZoneHalfHeightCm, 500.0);
	TestTrue(TEXT("Center cell"), Placement.CenterCell == FIntPoint(0, 96));
	TestTrue(TEXT("Rect minimum cell is the window minimum minus the border"), Placement.Rect.MinCell == FIntPoint(-128, -32));
	TestEqual(TEXT("Rect size"), Placement.Rect.Size, 256);
	TestEqual(TEXT("Ortho width is S times the cell size"), Placement.OrthoWidthCm, 256.0 * GroundCellSizeCm);
	TestEqual(TEXT("Capture x is the center cell boundary"), Placement.CaptureWorld.X, 0.0);
	TestEqual(TEXT("Capture y is the center cell boundary"), Placement.CaptureWorld.Y, 96.0 * GroundCellSizeCm);
	TestEqual(TEXT("Capture z is the zone top plus the height above"), Placement.CaptureWorld.Z, FloorZ + 2.0 * ZoneHalfHeightCm + 500.0);
	TestEqual(TEXT("Decode offset is capture z minus the floor"), Placement.DecodeOffsetCm, 800.0f);
	const double RectCenterX = (static_cast<double>(Placement.Rect.MinCell.X) + 0.5 * Placement.Rect.Size) * GroundCellSizeCm;
	const double RectCenterY = (static_cast<double>(Placement.Rect.MinCell.Y) + 0.5 * Placement.Rect.Size) * GroundCellSizeCm;
	TestTrue(TEXT("Capture xy is the rect center so texel (u, v) is the column minimum cell plus (u, v)"), RectCenterX == Placement.CaptureWorld.X && RectCenterY == Placement.CaptureWorld.Y);

	const FIntPoint FarWindow(640000 + 48, -640000 - 32);
	const double FarFloorZ = 123456.7;
	const FTDFog3DGroundCapturePlacement Far = FTDFog3DGroundLayout::ComputeCapturePlacement(FarWindow, 256, 64, GroundCellSizeCm, FarFloorZ, 300.0, 500.0);
	TestTrue(TEXT("Far rect minimum cell"), Far.Rect.MinCell == FarWindow - FIntPoint(64, 64));
	TestEqual(TEXT("Far rect size"), Far.Rect.Size, 384);
	TestEqual(TEXT("Far ortho width"), Far.OrthoWidthCm, 6000.0);
	TestEqual(TEXT("Far capture x"), Far.CaptureWorld.X, static_cast<double>(FarWindow.X + 128) * GroundCellSizeCm);
	TestEqual(TEXT("Far capture y"), Far.CaptureWorld.Y, static_cast<double>(FarWindow.Y + 128) * GroundCellSizeCm);
	TestTrue(TEXT("Far decode offset is subtracted in 64 bits before narrowing"), FMath::Abs(static_cast<double>(Far.DecodeOffsetCm) - 1100.0) <= 1.0e-4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DGroundCoverageTest,
	"TDGame.FluidFX.Fog3D.GroundCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DGroundCoverageTest::RunTest(const FString& Parameters)
{
	const int32 WindowCells = 128;
	const FIntPoint Window(-64, 32);

	const FTDFog3DGroundRect NarrowRect = FTDFog3DGroundLayout::ComputeCaptureRect(Window, WindowCells, 17);
	TestTrue(TEXT("Border 17 covers its own window and ring"), NarrowRect.CoversWindowWithRing(Window, WindowCells));
	TestTrue(TEXT("Border 17 covers a 16 cell move east"), NarrowRect.CoversWindowWithRing(Window + FIntPoint(16, 0), WindowCells));
	TestTrue(TEXT("Border 17 covers a 16 cell move west"), NarrowRect.CoversWindowWithRing(Window + FIntPoint(-16, 0), WindowCells));
	TestTrue(TEXT("Border 17 covers a 16 cell diagonal move"), NarrowRect.CoversWindowWithRing(Window + FIntPoint(16, -16), WindowCells));
	TestFalse(TEXT("Border 17 does not cover a 32 cell move north"), NarrowRect.CoversWindowWithRing(Window + FIntPoint(0, 32), WindowCells));
	TestFalse(TEXT("Border 17 does not cover a 32 cell move south"), NarrowRect.CoversWindowWithRing(Window + FIntPoint(0, -32), WindowCells));

	const FTDFog3DGroundRect RingOnlyRect = FTDFog3DGroundLayout::ComputeCaptureRect(Window, WindowCells, 1);
	TestTrue(TEXT("Border 1 covers exactly the window and ring"), RingOnlyRect.CoversWindowWithRing(Window, WindowCells));
	TestFalse(TEXT("Border 0 misses the ring"), FTDFog3DGroundLayout::ComputeCaptureRect(Window, WindowCells, 0).CoversWindowWithRing(Window, WindowCells));

	const FTDFog3DGroundRect WideRect = FTDFog3DGroundLayout::ComputeCaptureRect(Window, WindowCells, 64);
	TestTrue(TEXT("Border 64 covers a 48 cell move"), WideRect.CoversWindowWithRing(Window + FIntPoint(48, -48), WindowCells));
	TestFalse(TEXT("Border 64 does not cover a 64 cell move"), WideRect.CoversWindowWithRing(Window + FIntPoint(64, 0), WindowCells));
	TestFalse(TEXT("Border 64 does not cover a 64 cell move back"), WideRect.CoversWindowWithRing(Window + FIntPoint(0, -64), WindowCells));

	TestFalse(TEXT("An empty rect covers nothing"), FTDFog3DGroundRect().CoversWindowWithRing(Window, WindowCells));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DGroundTrackerStatesTest,
	"TDGame.FluidFX.Fog3D.GroundTrackerStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DGroundTrackerStatesTest::RunTest(const FString& Parameters)
{
	const int32 WindowCells = 128;
	const int32 BorderCells = 64;
	const int32 LatencyFrames = 2;
	FTDFog3DGroundTracker Tracker;
	TestTrue(TEXT("A new tracker has no image"), Tracker.GetState() == ETDFog3DGroundTrackerState::NoImage);

	const auto Advance = [&Tracker, WindowCells, BorderCells](const FIntPoint& Window, int32 Latency, uint64 Frame)
	{
		return Tracker.Advance(FTDFog3DGroundLayout::ComputeCaptureRect(Window, WindowCells, BorderCells), Window, WindowCells, Latency, Frame, true);
	};

	const FIntPoint FirstWindow(-64, -64);
	const FTDFog3DGroundRect FirstRect = FTDFog3DGroundLayout::ComputeCaptureRect(FirstWindow, WindowCells, BorderCells);
	const FTDFog3DGroundDecision FirstFrame = Advance(FirstWindow, LatencyFrames, 100);
	TestTrue(TEXT("First frame requests a capture"), FirstFrame.bRequestsCapture);
	TestFalse(TEXT("First frame holds the step"), FirstFrame.bAllowsStep);
	TestTrue(TEXT("First frame is requested"), Tracker.GetState() == ETDFog3DGroundTrackerState::Requested);
	TestTrue(TEXT("The request rect is the first capture rect"), Tracker.GetRequestRect() == FirstRect);

	const FTDFog3DGroundDecision WaitFrame = Advance(FirstWindow, LatencyFrames, 101);
	TestFalse(TEXT("The same window does not request again"), WaitFrame.bRequestsCapture);
	TestFalse(TEXT("One frame later the step still waits"), WaitFrame.bAllowsStep);

	const FTDFog3DGroundDecision ReadyFrame = Advance(FirstWindow, LatencyFrames, 102);
	TestTrue(TEXT("After the latency the capture is ready"), Tracker.GetState() == ETDFog3DGroundTrackerState::Ready);
	TestTrue(TEXT("The ready step converts the capture"), ReadyFrame.bConvertsCapture && ReadyFrame.bAllowsStep && ReadyFrame.StepRect == FirstRect);

	const FTDFog3DGroundDecision UnsubmittedFrame = Advance(FirstWindow, LatencyFrames, 103);
	TestTrue(TEXT("A ready capture without a submitted step converts on the next step"), UnsubmittedFrame.bConvertsCapture && UnsubmittedFrame.bAllowsStep);
	Tracker.ConfirmStep(UnsubmittedFrame);
	TestTrue(TEXT("The converted rect is the first rect"), Tracker.GetConvertedRect() == FirstRect);

	const FTDFog3DGroundDecision SteadyFrame = Advance(FirstWindow, LatencyFrames, 104);
	TestTrue(TEXT("After conversion steps run without converting again"), SteadyFrame.bAllowsStep && !SteadyFrame.bConvertsCapture && !SteadyFrame.bRequestsCapture);
	Tracker.ConfirmStep(SteadyFrame);

	const FIntPoint MovedWindow = FirstWindow + FIntPoint(16, 0);
	const FTDFog3DGroundRect MovedRect = FTDFog3DGroundLayout::ComputeCaptureRect(MovedWindow, WindowCells, BorderCells);
	const FTDFog3DGroundDecision MoveFrame = Advance(MovedWindow, LatencyFrames, 105);
	TestTrue(TEXT("A 16 cell move requests a new capture"), MoveFrame.bRequestsCapture);
	TestTrue(TEXT("A 16 cell move keeps stepping on the old ground"), MoveFrame.bAllowsStep && !MoveFrame.bConvertsCapture && MoveFrame.StepRect == FirstRect);
	TestTrue(TEXT("A 16 cell move is requested"), Tracker.GetState() == ETDFog3DGroundTrackerState::Requested);
	Tracker.ConfirmStep(MoveFrame);
	const FTDFog3DGroundDecision MoveWaitFrame = Advance(MovedWindow, LatencyFrames, 106);
	TestTrue(TEXT("One drawn frame after the move keeps stepping on the old ground"), MoveWaitFrame.bAllowsStep && !MoveWaitFrame.bConvertsCapture && MoveWaitFrame.StepRect == FirstRect);
	Tracker.ConfirmStep(MoveWaitFrame);
	const FTDFog3DGroundDecision MoveReadyFrame = Advance(MovedWindow, LatencyFrames, 107);
	TestTrue(TEXT("The moved capture converts after the latency"), MoveReadyFrame.bConvertsCapture && MoveReadyFrame.StepRect == MovedRect);
	Tracker.ConfirmStep(MoveReadyFrame);

	const FIntPoint TeleportWindow = MovedWindow + FIntPoint(4096, 0);
	TestTrue(TEXT("A teleport makes the layer reset"), FTDFog3DLayer::IsWindowJumpTooFar(MovedWindow, TeleportWindow, WindowCells));
	const FTDFog3DGroundDecision TeleportFrame = Advance(TeleportWindow, LatencyFrames, 110);
	TestTrue(TEXT("A teleport requests a capture"), TeleportFrame.bRequestsCapture);
	TestFalse(TEXT("A teleport holds the step"), TeleportFrame.bAllowsStep);
	TestFalse(TEXT("A teleport still holds one frame later"), Advance(TeleportWindow, LatencyFrames, 111).bAllowsStep);
	const FTDFog3DGroundDecision TeleportReadyFrame = Advance(TeleportWindow, LatencyFrames, 112);
	TestTrue(TEXT("A teleport converts after the latency"), TeleportReadyFrame.bConvertsCapture && TeleportReadyFrame.bAllowsStep);
	Tracker.ConfirmStep(TeleportReadyFrame);

	const FIntPoint FirstStepWindow = TeleportWindow + FIntPoint(0, 16);
	const FIntPoint SecondStepWindow = FirstStepWindow + FIntPoint(0, 16);
	const FTDFog3DGroundRect SecondStepRect = FTDFog3DGroundLayout::ComputeCaptureRect(SecondStepWindow, WindowCells, BorderCells);
	TestTrue(TEXT("First of two quick moves requests"), Advance(FirstStepWindow, LatencyFrames, 120).bRequestsCapture);
	const FTDFog3DGroundDecision SupersedeFrame = Advance(SecondStepWindow, LatencyFrames, 121);
	TestTrue(TEXT("A second quick move supersedes the pending request"), SupersedeFrame.bRequestsCapture && Tracker.GetRequestFrame() == 121);
	const FTDFog3DGroundDecision SupersedeWaitFrame = Advance(SecondStepWindow, LatencyFrames, 122);
	TestTrue(TEXT("The superseded request is not converted early"), !SupersedeWaitFrame.bConvertsCapture && SupersedeWaitFrame.bAllowsStep);
	const FTDFog3DGroundDecision SupersedeReadyFrame = Advance(SecondStepWindow, LatencyFrames, 123);
	TestTrue(TEXT("The latest request converts after its own latency"), SupersedeReadyFrame.bConvertsCapture && SupersedeReadyFrame.StepRect == SecondStepRect);
	Tracker.ConfirmStep(SupersedeReadyFrame);

	Tracker.Reset();
	TestTrue(TEXT("Leaving the zone forgets the image"), Tracker.GetState() == ETDFog3DGroundTrackerState::NoImage && !Tracker.GetConvertedRect().IsValid());
	const FTDFog3DGroundDecision ReentryFrame = Advance(SecondStepWindow, LatencyFrames, 130);
	TestTrue(TEXT("Re-entry requests the same rect again"), ReentryFrame.bRequestsCapture);
	TestFalse(TEXT("Re-entry holds the step"), ReentryFrame.bAllowsStep);

	FTDFog3DGroundTracker QuickTracker;
	QuickTracker.Advance(FirstRect, FirstWindow, WindowCells, 1, 200, true);
	const FTDFog3DGroundDecision QuickReady = QuickTracker.Advance(FirstRect, FirstWindow, WindowCells, 1, 201, true);
	TestTrue(TEXT("Latency one converts on the next frame"), QuickReady.bConvertsCapture);

	FTDFog3DGroundTracker EmptyTracker;
	const FTDFog3DGroundDecision EmptyDecision = EmptyTracker.Advance(FTDFog3DGroundRect(), FirstWindow, WindowCells, 1, 300, true);
	TestTrue(TEXT("An empty desired rect neither requests nor steps"), !EmptyDecision.bRequestsCapture && !EmptyDecision.bAllowsStep);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DGroundTrackerDrawnFramesTest,
	"TDGame.FluidFX.Fog3D.GroundTrackerDrawnFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DGroundTrackerDrawnFramesTest::RunTest(const FString& Parameters)
{
	const int32 WindowCells = 128;
	const FIntPoint Window(-64, -64);
	const FTDFog3DGroundRect Rect = FTDFog3DGroundLayout::ComputeCaptureRect(Window, WindowCells, 64);

	FTDFog3DGroundTracker Tracker;
	TestTrue(TEXT("The first advance requests even when nothing was drawn"), Tracker.Advance(Rect, Window, WindowCells, 2, 100, false).bRequestsCapture);
	bool bHeldWhileUndrawn = true;
	for (uint64 Frame = 101; Frame <= 120; ++Frame)
	{
		const FTDFog3DGroundDecision Undrawn = Tracker.Advance(Rect, Window, WindowCells, 2, Frame, false);
		bHeldWhileUndrawn &= !Undrawn.bAllowsStep && !Undrawn.bConvertsCapture;
	}
	TestTrue(TEXT("Twenty frames without a scene draw never convert"), bHeldWhileUndrawn);
	TestTrue(TEXT("Undrawn frames leave the request waiting"), Tracker.GetState() == ETDFog3DGroundTrackerState::Requested && Tracker.GetDrawnFramesSinceRequest() == 0);

	const FTDFog3DGroundDecision FirstDrawn = Tracker.Advance(Rect, Window, WindowCells, 2, 121, true);
	TestTrue(TEXT("One drawn frame is not enough for latency two"), !FirstDrawn.bConvertsCapture && Tracker.GetDrawnFramesSinceRequest() == 1);
	TestFalse(TEXT("An undrawn frame between draws does not count"), Tracker.Advance(Rect, Window, WindowCells, 2, 122, false).bConvertsCapture);
	const FTDFog3DGroundDecision SecondDrawn = Tracker.Advance(Rect, Window, WindowCells, 2, 123, true);
	TestTrue(TEXT("The second drawn frame converts"), SecondDrawn.bConvertsCapture && SecondDrawn.bAllowsStep && SecondDrawn.StepRect == Rect);
	Tracker.ConfirmStep(SecondDrawn);
	TestEqual(TEXT("The request frame is kept for the log"), Tracker.GetRequestFrame(), static_cast<uint64>(100));

	const FIntPoint MovedWindow = Window + FIntPoint(16, 0);
	const FTDFog3DGroundRect MovedRect = FTDFog3DGroundLayout::ComputeCaptureRect(MovedWindow, WindowCells, 64);
	const FTDFog3DGroundDecision MoveFrame = Tracker.Advance(MovedRect, MovedWindow, WindowCells, 2, 124, true);
	TestTrue(TEXT("A new request starts its own drawn count"), MoveFrame.bRequestsCapture && Tracker.GetDrawnFramesSinceRequest() == 0);
	TestTrue(TEXT("The drawn frame of the request tick is not counted"), MoveFrame.bAllowsStep && !MoveFrame.bConvertsCapture);
	TestFalse(TEXT("The new request needs its own two draws"), Tracker.Advance(MovedRect, MovedWindow, WindowCells, 2, 125, true).bConvertsCapture);
	TestTrue(TEXT("The new request converts after two draws"), Tracker.Advance(MovedRect, MovedWindow, WindowCells, 2, 126, true).bConvertsCapture);

	TestTrue(TEXT("A standalone capture is always drawn"), ATDFog3DZoneActor::IsGroundCaptureDrawn(false, 7, 7));
	TestFalse(TEXT("A main renderer capture is not drawn while the scene frame number stays"), ATDFog3DZoneActor::IsGroundCaptureDrawn(true, 7, 7));
	TestTrue(TEXT("A main renderer capture is drawn once the scene frame number moves"), ATDFog3DZoneActor::IsGroundCaptureDrawn(true, 8, 7));
	TestTrue(TEXT("A wrapped scene frame number still counts as drawn"), ATDFog3DZoneActor::IsGroundCaptureDrawn(true, 0, MAX_uint32));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DGroundZoneRestartsTest,
	"TDGame.FluidFX.Fog3D.GroundZoneRestarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DGroundZoneRestartsTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DGroundTestPrivate;

	const FTDFog3DSettings Base;
	struct FTDFog3DGroundChangeCase
	{
		const TCHAR* Name;
		TFunction<void(FTDFog3DSettings&)> Mutate;
		bool bIsGroundChange;
	};
	const TArray<FTDFog3DGroundChangeCase> ChangeCases = {
		{ TEXT("ground following"), [](FTDFog3DSettings& S) { S.bFollowsGround = !S.bFollowsGround; }, true },
		{ TEXT("wall step"), [](FTDFog3DSettings& S) { S.GroundWallStepCm += 1.0f; }, true },
		{ TEXT("capture border"), [](FTDFog3DSettings& S) { S.GroundCaptureBorderCells += 1; }, true },
		{ TEXT("capture height above"), [](FTDFog3DSettings& S) { S.GroundCaptureHeightAboveZoneCm += 1.0f; }, true },
		{ TEXT("capture latency"), [](FTDFog3DSettings& S) { S.GroundCaptureLatencyFrames += 1; }, true },
		{ TEXT("missing height"), [](FTDFog3DSettings& S) { S.GroundMissingHeightCm += 1.0f; }, true },
		{ TEXT("missing below floor margin"), [](FTDFog3DSettings& S) { S.GroundMissingBelowFloorCm += 1.0f; }, true },
		{ TEXT("base density"), [](FTDFog3DSettings& S) { S.BaseDensity += 0.25f; }, false },
		{ TEXT("ground material path"), [](FTDFog3DSettings& S) { S.GroundFollowVolumeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Scribbled/M_Scribbled.M_Scribbled"))); }, false },
	};
	TestFalse(TEXT("The same settings are no ground change"), ATDFog3DZoneActor::HaveGroundSettingsChanged(Base, Base));
	for (const FTDFog3DGroundChangeCase& ChangeCase : ChangeCases)
	{
		FTDFog3DSettings Changed = Base;
		ChangeCase.Mutate(Changed);
		TestEqual(*FString::Printf(TEXT("Ground change detection: %s"), ChangeCase.Name), ATDFog3DZoneActor::HaveGroundSettingsChanged(Base, Changed), ChangeCase.bIsGroundChange);
	}

	AddExpectedMessagePlain(TEXT("has no VolumeMaterial and stays hidden"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("Fog3D zone summary:"), ELogVerbosity::Display, EAutomationExpectedMessageFlags::Contains, 1);

	FTDFog3DGroundTestWorldScope WorldScope;
	UWorld& World = *WorldScope.World;
	ATDFog3DZoneActor* Zone = World.SpawnActor<ATDFog3DZoneActor>(FVector(0.0, 0.0, 150.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("The zone spawns"), Zone))
	{
		return false;
	}
	FTDFog3DSettings& Settings = FTDFog3DZoneActorGroundTestAccess::GetSettings(*Zone);
	FTDFog3DGroundTracker& Tracker = FTDFog3DZoneActorGroundTestAccess::GetGroundTracker(*Zone);
	const auto ConvertTracker = [&Tracker, Zone]()
	{
		const int32 Resolution = FTDFog3DZoneActorGroundTestAccess::GetLayerResolution(*Zone);
		const FIntPoint Window(-64, -64);
		const FTDFog3DGroundRect Rect = FTDFog3DGroundLayout::ComputeCaptureRect(Window, Resolution, FTDFog3DZoneActorGroundTestAccess::GetSettings(*Zone).GroundCaptureBorderCells);
		Tracker.Advance(Rect, Window, Resolution, 1, 1, true);
		const FTDFog3DGroundDecision Ready = Tracker.Advance(Rect, Window, Resolution, 1, 2, true);
		Tracker.ConfirmStep(Ready);
		return Ready.bConvertsCapture && Tracker.GetConvertedRect().IsValid();
	};

	FTDFog3DZoneActorGroundTestAccess::RestartGroundWhenSettingsChange(*Zone, Base);
	TestTrue(TEXT("Without ground resources a settings change leaves the empty tracker alone"), Tracker.GetState() == ETDFog3DGroundTrackerState::NoImage);

	Settings.bFollowsGround = true;
	FTDFog3DZoneActorGroundTestAccess::UpdateGroundResources(*Zone);
	if (!TestNotNull(TEXT("Ground on creates the capture"), FTDFog3DZoneActorGroundTestAccess::GetGroundCapture(*Zone)) || !TestTrue(TEXT("The tracker reaches a converted image"), ConvertTracker()))
	{
		World.DestroyActor(Zone);
		return false;
	}
	FTDFog3DZoneActorGroundTestAccess::RestartGroundWhenSettingsChange(*Zone, Settings);
	TestTrue(TEXT("A re-read with the same ground settings keeps the image"), Tracker.GetState() == ETDFog3DGroundTrackerState::Ready && Tracker.GetConvertedRect().IsValid());
	FTDFog3DSettings OtherWallStep = Settings;
	OtherWallStep.GroundWallStepCm += 5.0f;
	FTDFog3DZoneActorGroundTestAccess::RestartGroundWhenSettingsChange(*Zone, OtherWallStep);
	TestTrue(TEXT("A ground settings change forgets the image so the next step recaptures and rebakes"), Tracker.GetState() == ETDFog3DGroundTrackerState::NoImage && !Tracker.GetConvertedRect().IsValid());

	TestTrue(TEXT("The tracker converts again"), ConvertTracker());
	const int32 FirstResolution = FTDFog3DZoneActorGroundTestAccess::GetLayerResolution(*Zone);
	Settings.DefaultQualityLevel = Settings.DefaultQualityLevel == 0 ? 2 : 0;
	FTDFog3DZoneActorGroundTestAccess::ConfigureLayer(*Zone);
	const int32 SwitchedResolution = FTDFog3DZoneActorGroundTestAccess::GetLayerResolution(*Zone);
	TestNotEqual(TEXT("The quality switch changes the window size"), SwitchedResolution, FirstResolution);
	UTextureRenderTarget2D* SwitchedTarget = FTDFog3DZoneActorGroundTestAccess::GetGroundCaptureTarget(*Zone);
	UTextureRenderTarget2D* SwitchedMaterialTarget = FTDFog3DZoneActorGroundTestAccess::GetGroundMaterialTarget(*Zone);
	const int32 SwitchedSize = SwitchedResolution + 2 * Settings.GroundCaptureBorderCells;
	TestTrue(TEXT("The quality switch resizes the capture target to N + 2B"), SwitchedTarget && SwitchedTarget->SizeX == SwitchedSize && SwitchedTarget->SizeY == SwitchedSize);
	TestTrue(TEXT("The quality switch resizes the material target"), SwitchedMaterialTarget && SwitchedMaterialTarget->SizeX == SwitchedSize);
	TestTrue(TEXT("The capture writes the resized target"), FTDFog3DZoneActorGroundTestAccess::GetGroundCapture(*Zone)->TextureTarget == SwitchedTarget);
	TestTrue(TEXT("The quality switch forgets the image"), Tracker.GetState() == ETDFog3DGroundTrackerState::NoImage);

	TestTrue(TEXT("The tracker converts at the new size"), ConvertTracker());
	FTDFog3DZoneActorGroundTestAccess::RestartGroundAfterRejection(*Zone);
	TestTrue(TEXT("Without a new GPU rejection the image stays"), Tracker.GetState() == ETDFog3DGroundTrackerState::Ready);
	FTDFog3DZoneActorGroundTestAccess::SetGroundSeenRejectedStepCount(*Zone, -1);
	FTDFog3DZoneActorGroundTestAccess::RestartGroundAfterRejection(*Zone);
	TestTrue(TEXT("A rejection count above the seen count forgets the image"), Tracker.GetState() == ETDFog3DGroundTrackerState::NoImage);
	World.DestroyActor(Zone);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DGroundFrameInputAssemblyTest,
	"TDGame.FluidFX.Fog3D.GroundFrameInputAssembly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DGroundFrameInputAssemblyTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DGroundTestPrivate;

	FTDFog3DSettings Settings;
	Settings.GroundWallStepCm = 25.0f;
	Settings.GroundMissingBelowFloorCm = 7500.0f;
	FTDFog3DLayer Layer;
	Layer.Configure(Settings, 0, 2);

	FTDFog3DStepRequest Request;
	Request.PlayerWorld = FVector(100.0, -200.0, 50.0);
	Request.Clock = MakeStepClock();
	TestTrue(TEXT("The preview window is the window the step uses"), Layer.PreviewNextWindowMinCell(Request.PlayerWorld) == Layer.BuildFrameInput(Request).WindowMinCell);

	const FTDFluidGpuFog3DFrameInput Off = Layer.BuildFrameInput(Request);
	TestFalse(TEXT("Ground off by default"), Off.bFollowsGround);
	TestFalse(TEXT("Ground off is never changed"), Off.bGroundChanged);
	TestNull(TEXT("Ground off carries no capture"), Off.GroundCapture.DepthTarget);
	TestNull(TEXT("Ground off carries no material target"), Off.GroundMaterialTarget);
	TestNull(TEXT("Ground off carries no upload"), Off.GroundUpload);

	FTextureRenderTargetResource* CaptureResource = MakeSentinelResource(0x1000);
	FTextureRenderTargetResource* MaterialResource = MakeSentinelResource(0x2000);

	FTDFog3DStepRequest OffChangedRequest = Request;
	OffChangedRequest.Ground.bGroundChanged = true;
	OffChangedRequest.Ground.CaptureDepthTarget = CaptureResource;
	const FTDFluidGpuFog3DFrameInput OffChanged = Layer.BuildFrameInput(OffChangedRequest);
	TestTrue(TEXT("A changed flag without ground following is dropped"), !OffChanged.bFollowsGround && !OffChanged.bGroundChanged && OffChanged.GroundCapture.DepthTarget == nullptr);

	FTDFog3DStepRequest SteadyRequest = Request;
	SteadyRequest.Ground.bFollowsGround = true;
	SteadyRequest.Ground.CaptureDepthTarget = CaptureResource;
	SteadyRequest.Ground.MaterialTarget = MaterialResource;
	const FTDFluidGpuFog3DFrameInput Steady = Layer.BuildFrameInput(SteadyRequest);
	TestTrue(TEXT("Steady ground step follows the ground"), Steady.bFollowsGround);
	TestFalse(TEXT("Steady ground step does not change the ground"), Steady.bGroundChanged);
	TestEqual(TEXT("Wall step comes from the settings"), Steady.GroundWallStepCm, 25.0f);
	TestNull(TEXT("Steady ground step carries no capture"), Steady.GroundCapture.DepthTarget);
	TestNull(TEXT("Steady ground step carries no material target"), Steady.GroundMaterialTarget);

	const FTDFog3DGroundRect CaptureRect = FTDFog3DGroundLayout::ComputeCaptureRect(Steady.WindowMinCell, Layer.GetResolution(), 64);
	FTDFog3DStepRequest ChangedRequest = SteadyRequest;
	ChangedRequest.Ground.bGroundChanged = true;
	ChangedRequest.Ground.CaptureRect = CaptureRect;
	ChangedRequest.Ground.DecodeOffsetCm = 800.0f;
	ChangedRequest.Ground.MissingHeightCm = -12.5f;
	const FTDFluidGpuFog3DFrameInput Changed = Layer.BuildFrameInput(ChangedRequest);
	TestTrue(TEXT("Changed ground step follows and changes the ground"), Changed.bFollowsGround && Changed.bGroundChanged);
	TestTrue(TEXT("Capture target is carried"), Changed.GroundCapture.DepthTarget == CaptureResource);
	TestTrue(TEXT("Capture rect minimum cell is carried"), Changed.GroundCapture.MinCell == CaptureRect.MinCell);
	TestEqual(TEXT("Capture size is carried"), Changed.GroundCapture.Size, 256);
	TestEqual(TEXT("Decode offset is carried"), Changed.GroundCapture.DecodeOffsetCm, 800.0f);
	TestEqual(TEXT("Missing height is carried"), Changed.GroundCapture.MissingHeightCm, -12.5f);
	TestEqual(TEXT("Missing below floor margin comes from the settings"), Changed.GroundCapture.MissingBelowFloorCm, 7500.0f);
	TestTrue(TEXT("Material target is carried"), Changed.GroundMaterialTarget == MaterialResource);
	TestNull(TEXT("The game path never uploads"), Changed.GroundUpload);
	TestTrue(TEXT("The capture rect covers the step window and ring"), CaptureRect.CoversWindowWithRing(Changed.WindowMinCell, Changed.Resolution));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DGroundMaterialChoiceTest,
	"TDGame.FluidFX.Fog3D.GroundMaterialChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DGroundMaterialChoiceTest::RunTest(const FString& Parameters)
{
	const TSoftObjectPtr<UMaterialInterface> ActorMaterial(FSoftObjectPath(TEXT("/Game/FluidFX/Materials/M_TD_Fog3DVolume.M_TD_Fog3DVolume")));
	const TSoftObjectPtr<UMaterialInterface> GroundMaterial(FSoftObjectPath(TEXT("/Game/FluidFX/Materials/M_TD_Fog3DVolumeGround.M_TD_Fog3DVolumeGround")));

	FTDFog3DSettings Settings;
	TestTrue(TEXT("Ground off with no ground path uses the actor material"), ATDFog3DZoneActor::ChooseVolumeMaterial(Settings, ActorMaterial) == ActorMaterial);
	Settings.GroundFollowVolumeMaterial = GroundMaterial;
	TestTrue(TEXT("Ground off with a ground path still uses the actor material"), ATDFog3DZoneActor::ChooseVolumeMaterial(Settings, ActorMaterial) == ActorMaterial);
	Settings.bFollowsGround = true;
	TestTrue(TEXT("Ground on with a ground path uses the ground material"), ATDFog3DZoneActor::ChooseVolumeMaterial(Settings, ActorMaterial) == GroundMaterial);
	Settings.GroundFollowVolumeMaterial.Reset();
	TestTrue(TEXT("Ground on with an empty ground path uses the actor material"), ATDFog3DZoneActor::ChooseVolumeMaterial(Settings, ActorMaterial) == ActorMaterial);
	TestTrue(TEXT("Ground on with no materials at all chooses nothing"), ATDFog3DZoneActor::ChooseVolumeMaterial(Settings, TSoftObjectPtr<UMaterialInterface>()).IsNull());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DGroundMaterialParametersTest,
	"TDGame.FluidFX.Fog3D.GroundMaterialParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DGroundMaterialParametersTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DGroundTestPrivate;

	FTDFog3DGroundRect SimpleRect;
	SimpleRect.MinCell = FIntPoint(-128, 32);
	SimpleRect.Size = 256;
	const FVector4f Simple = FTDFog3DGroundLayout::ComputeMaterialGround(SimpleRect, GroundCellSizeCm, FVector(100.0, 200.0, 150.0));
	TestTrue(*FString::Printf(TEXT("Simple ground vector (%f, %f, %f, %f)"), Simple.X, Simple.Y, Simple.Z, Simple.W), IsNear(Simple, FVector4f(-128.0f * 15.625f - 100.0f, 32.0f * 15.625f - 200.0f, 256.0f, 256.0f), 1.0e-3f));

	FTDFog3DGroundRect FarRect;
	FarRect.MinCell = FIntPoint(640000 - 16, -640000 + 48);
	FarRect.Size = 384;
	const FVector FarReference(1.0e7 + 3.37, -1.0e7 - 1.21, 500.0);
	const FVector4f Far = FTDFog3DGroundLayout::ComputeMaterialGround(FarRect, GroundCellSizeCm, FarReference);
	TestTrue(TEXT("Far ground x subtracts in 64 bits"), FMath::Abs(static_cast<double>(Far.X) - (static_cast<double>(FarRect.MinCell.X) * GroundCellSizeCm - FarReference.X)) < 0.01);
	TestTrue(TEXT("Far ground y subtracts in 64 bits"), FMath::Abs(static_cast<double>(Far.Y) - (static_cast<double>(FarRect.MinCell.Y) * GroundCellSizeCm - FarReference.Y)) < 0.01);
	TestTrue(TEXT("Far ground size"), Far.Z == 384.0f && Far.W == 384.0f);

	AddExpectedMessagePlain(TEXT("has no VolumeMaterial and stays hidden"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	AddExpectedMessage(TEXT("Fog3D zone summary:"), ELogVerbosity::Display, EAutomationExpectedMessageFlags::Contains, 2);

	FTDFog3DGroundTestWorldScope WorldScope;
	UWorld& World = *WorldScope.World;
	ATDFog3DZoneActor* Zone = World.SpawnActor<ATDFog3DZoneActor>(FVector(0.0, 0.0, 150.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("The zone spawns"), Zone))
	{
		return false;
	}
	TestNull(TEXT("Ground off in the table creates no capture"), FTDFog3DZoneActorGroundTestAccess::GetGroundCapture(*Zone));
	TestNull(TEXT("Ground off in the table creates no capture target"), FTDFog3DZoneActorGroundTestAccess::GetGroundCaptureTarget(*Zone));
	TestNull(TEXT("Ground off in the table creates no material target"), FTDFog3DZoneActorGroundTestAccess::GetGroundMaterialTarget(*Zone));

	FTDFog3DZoneActorGroundTestAccess::GetSettings(*Zone).bFollowsGround = true;
	FTDFog3DZoneActorGroundTestAccess::UpdateGroundResources(*Zone);
	const int32 Resolution = FTDFog3DZoneActorGroundTestAccess::GetLayerResolution(*Zone);
	const int32 ExpectedSize = Resolution + 2 * FTDFog3DZoneActorGroundTestAccess::GetSettings(*Zone).GroundCaptureBorderCells;
	USceneCaptureComponent2D* Capture = FTDFog3DZoneActorGroundTestAccess::GetGroundCapture(*Zone);
	UTextureRenderTarget2D* CaptureTarget = FTDFog3DZoneActorGroundTestAccess::GetGroundCaptureTarget(*Zone);
	UTextureRenderTarget2D* MaterialTarget = FTDFog3DZoneActorGroundTestAccess::GetGroundMaterialTarget(*Zone);
	if (!TestNotNull(TEXT("Ground on creates the capture"), Capture) || !TestNotNull(TEXT("Ground on creates the capture target"), CaptureTarget) || !TestNotNull(TEXT("Ground on creates the material target"), MaterialTarget))
	{
		World.DestroyActor(Zone);
		return false;
	}
	TestTrue(TEXT("Capture is orthographic"), Capture->ProjectionType == ECameraProjectionMode::Orthographic);
	TestTrue(TEXT("Capture reads scene depth"), Capture->CaptureSource == ESceneCaptureSource::SCS_SceneDepth);
	TestFalse(TEXT("Capture does not run every frame"), Capture->bCaptureEveryFrame);
	TestFalse(TEXT("Capture does not run on movement"), Capture->bCaptureOnMovement);
	TestTrue(TEXT("Capture writes the capture target"), Capture->TextureTarget == CaptureTarget);
	TestTrue(TEXT("Capture is registered"), Capture->IsRegistered());
	TestTrue(TEXT("Capture is transient"), Capture->HasAnyFlags(RF_Transient));
	TestEqual(TEXT("Capture target width is S"), CaptureTarget->SizeX, ExpectedSize);
	TestEqual(TEXT("Capture target height is S"), CaptureTarget->SizeY, ExpectedSize);
	TestTrue(TEXT("Capture target is R32F"), CaptureTarget->RenderTargetFormat == RTF_R32f);
	TestEqual(TEXT("Capture target clears to 1e9"), CaptureTarget->ClearColor.R, 1.0e9f);
	TestTrue(TEXT("Capture target is transient"), CaptureTarget->HasAnyFlags(RF_Transient));
	TestEqual(TEXT("Material target width is S"), MaterialTarget->SizeX, ExpectedSize);
	TestTrue(TEXT("Material target is R32F"), MaterialTarget->RenderTargetFormat == RTF_R32f);
	TestTrue(TEXT("Material target clamps at the edges"), MaterialTarget->AddressX == TA_Clamp && MaterialTarget->AddressY == TA_Clamp);
	TestTrue(TEXT("Material target filters bilinearly"), MaterialTarget->Filter == TF_Bilinear);
	TestTrue(TEXT("Material target is transient"), MaterialTarget->HasAnyFlags(RF_Transient));

	FTDFog3DGroundDecision SteadyDecision;
	SteadyDecision.bAllowsStep = true;
	const FTDFog3DGroundStep SteadyStep = FTDFog3DZoneActorGroundTestAccess::MakeGroundStep(*Zone, SteadyDecision);
	TestTrue(TEXT("A steady ground step follows without resources"), SteadyStep.bFollowsGround && !SteadyStep.bGroundChanged && !SteadyStep.CaptureDepthTarget && !SteadyStep.MaterialTarget);
	FTDFog3DGroundDecision ConvertDecision;
	ConvertDecision.bAllowsStep = true;
	ConvertDecision.bConvertsCapture = true;
	ConvertDecision.StepRect = FTDFog3DGroundLayout::ComputeCaptureRect(FIntPoint(-64, -64), Resolution, FTDFog3DZoneActorGroundTestAccess::GetSettings(*Zone).GroundCaptureBorderCells);
	FTDFog3DZoneActorGroundTestAccess::SetGroundCaptureDecodeOffset(*Zone, 800.0f);
	const FTDFog3DGroundStep ConvertStep = FTDFog3DZoneActorGroundTestAccess::MakeGroundStep(*Zone, ConvertDecision);
	TestTrue(TEXT("A converting step changes the ground"), ConvertStep.bFollowsGround && ConvertStep.bGroundChanged);
	TestTrue(TEXT("A converting step carries the capture resource"), ConvertStep.CaptureDepthTarget == CaptureTarget->GameThread_GetRenderTargetResource());
	TestTrue(TEXT("A converting step carries the material resource"), ConvertStep.MaterialTarget == MaterialTarget->GameThread_GetRenderTargetResource());
	TestTrue(TEXT("A converting step carries the rect"), ConvertStep.CaptureRect == ConvertDecision.StepRect);
	TestEqual(TEXT("A converting step carries the decode offset"), ConvertStep.DecodeOffsetCm, 800.0f);
	TestEqual(TEXT("A converting step carries the missing height"), ConvertStep.MissingHeightCm, FTDFog3DZoneActorGroundTestAccess::GetSettings(*Zone).GroundMissingHeightCm);

	UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), Zone);
	if (!TestNotNull(TEXT("A material instance is created"), MaterialInstance))
	{
		World.DestroyActor(Zone);
		return false;
	}
	FTDFog3DZoneActorGroundTestAccess::AttachMaterialInstance(*Zone, MaterialInstance);
	UTexture* GroundTexture = nullptr;
	TestTrue(TEXT("Fog3DGroundHeight is written"), MaterialInstance->GetTextureParameterValue(FName(TEXT("Fog3DGroundHeight")), GroundTexture));
	TestTrue(TEXT("Fog3DGroundHeight is the material copy target"), GroundTexture == MaterialTarget);

	const FVector4f ExpectedGround = FTDFog3DGroundLayout::ComputeMaterialGround(ConvertDecision.StepRect, GroundCellSizeCm, FVector(0.0, 0.0, 150.0));
	FTDFog3DZoneActorGroundTestAccess::GetDesiredMaterialParameters(*Zone).Ground = ExpectedGround;
	FTDFog3DZoneActorGroundTestAccess::WriteMaterialParameters(*Zone);
	FLinearColor GroundValue;
	if (TestTrue(TEXT("Fog3DGround is written"), MaterialInstance->GetVectorParameterValue(FName(TEXT("Fog3DGround")), GroundValue)))
	{
		TestTrue(TEXT("Fog3DGround holds the rect corner and size"), IsNear(FVector4f(GroundValue.R, GroundValue.G, GroundValue.B, GroundValue.A), ExpectedGround, 1.0e-3f));
	}

	FTDFog3DZoneActorGroundTestAccess::GetSettings(*Zone).bFollowsGround = false;
	FTDFog3DZoneActorGroundTestAccess::UpdateGroundResources(*Zone);
	TestNull(TEXT("Ground off drops the capture"), FTDFog3DZoneActorGroundTestAccess::GetGroundCapture(*Zone));
	TestNull(TEXT("Ground off drops the capture target"), FTDFog3DZoneActorGroundTestAccess::GetGroundCaptureTarget(*Zone));
	TestNull(TEXT("Ground off drops the material target"), FTDFog3DZoneActorGroundTestAccess::GetGroundMaterialTarget(*Zone));
	TestFalse(TEXT("The dropped capture is unregistered"), Capture->IsRegistered());
	UTexture* GroundTextureAfterOff = nullptr;
	MaterialInstance->GetTextureParameterValue(FName(TEXT("Fog3DGroundHeight")), GroundTextureAfterOff);
	TestTrue(TEXT("Ground off releases the material copy target from the material instance"), GroundTextureAfterOff != MaterialTarget);
	FLinearColor GroundValueAfterOff;
	TestTrue(TEXT("Ground off rewrites the material parameters"), MaterialInstance->GetVectorParameterValue(FName(TEXT("Fog3DGround")), GroundValueAfterOff) && GroundValueAfterOff == FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	const FTDFog3DGroundStep OffStep = FTDFog3DZoneActorGroundTestAccess::MakeGroundStep(*Zone, ConvertDecision);
	TestFalse(TEXT("Without resources a step does not follow the ground"), OffStep.bFollowsGround || OffStep.bGroundChanged);

	FTDFog3DZoneActorGroundTestAccess::GetSettings(*Zone).bFollowsGround = true;
	FTDFog3DZoneActorGroundTestAccess::UpdateGroundResources(*Zone);
	TestNotNull(TEXT("Ground on again creates the capture again"), FTDFog3DZoneActorGroundTestAccess::GetGroundCapture(*Zone));
	World.DestroyActor(Zone);
	TestNull(TEXT("End play drops the capture"), FTDFog3DZoneActorGroundTestAccess::GetGroundCapture(*Zone));
	TestNull(TEXT("End play drops the material target"), FTDFog3DZoneActorGroundTestAccess::GetGroundMaterialTarget(*Zone));

	ATDFog3DZoneActor* RestingZone = World.SpawnActor<ATDFog3DZoneActor>(FVector(0.0, 0.0, 150.0), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("A second zone spawns"), RestingZone))
	{
		FTDFog3DZoneActorGroundTestAccess::GetSettings(*RestingZone).bFollowsGround = true;
		FTDFog3DZoneActorGroundTestAccess::UpdateGroundResources(*RestingZone);
		TestNotNull(TEXT("The new representative may follow the ground"), FTDFog3DZoneActorGroundTestAccess::GetGroundCapture(*RestingZone));
		World.DestroyActor(RestingZone);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DTerrainCaptureListSharedTest,
	"TDGame.FluidFX.Fog3D.TerrainCaptureListShared",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFog3DTerrainCaptureListSharedTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DGroundTestPrivate;

	FTDFog3DGroundTestWorldScope WorldScope;
	UWorld& World = *WorldScope.World;
	AStaticMeshActor* TerrainActor = World.SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	AStaticMeshActor* BoxActor = World.SpawnActor<AStaticMeshActor>(FVector(500.0, 0.0, 0.0), FRotator::ZeroRotator);
	APawn* Pawn = World.SpawnActor<APawn>(FVector(0.0, 500.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Terrain actor spawns"), TerrainActor) || !TestNotNull(TEXT("Box actor spawns"), BoxActor) || !TestNotNull(TEXT("Pawn spawns"), Pawn))
	{
		return false;
	}
	TerrainActor->Tags.Add(FTDFluidTerrainCapture::TerrainActorTag);
	TestTrue(TEXT("The shared terrain tag is TDFluidTerrain"), FTDFluidTerrainCapture::TerrainActorTag == FName(TEXT("TDFluidTerrain")));

	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(BoxActor, NAME_None, RF_Transient);
	Capture->ShowOnlyActors.Add(Pawn);
	FTDFluidTerrainCapture::ApplyTerrainOnlyList(*Capture, World);
	TestTrue(TEXT("The list uses the show only list"), Capture->PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList);
	TestFalse(TEXT("Without a game viewport the capture uses its own renderer"), Capture->bRenderInMainRenderer);
	TestTrue(TEXT("The tagged terrain actor is listed"), Capture->ShowOnlyActors.Contains(TerrainActor));
	TestFalse(TEXT("An untagged box is not listed"), Capture->ShowOnlyActors.Contains(BoxActor));
	TestFalse(TEXT("The previous list is cleared"), Capture->ShowOnlyActors.Contains(Pawn));
	TestEqual(TEXT("Only the tagged actor is listed"), Capture->ShowOnlyActors.Num(), 1);

	FTDFluidTerrainCapture::HideOwnerAndPawns(*Capture, BoxActor, true, World);
	TestTrue(TEXT("The owner is hidden"), Capture->HiddenActors.Contains(BoxActor));
	TestTrue(TEXT("Pawns are hidden when asked"), Capture->HiddenActors.Contains(Pawn));
	FTDFluidTerrainCapture::HideOwnerAndPawns(*Capture, BoxActor, false, World);
	TestTrue(TEXT("Pawns stay visible when not asked"), Capture->HiddenActors.Contains(BoxActor) && !Capture->HiddenActors.Contains(Pawn));

	const ULandscapeComponent* LandscapeComponent = GetDefault<ULandscapeComponent>();
	UStaticMeshComponent* MeshComponent = TerrainActor->GetStaticMeshComponent();
	TestTrue(TEXT("A landscape component is a terrain surface"), FTDFluidTerrainCapture::IsLandscapeSurfaceComponent(*LandscapeComponent));
	if (TestNotNull(TEXT("The terrain actor has a mesh component"), MeshComponent))
	{
		TestFalse(TEXT("A static mesh component is not a landscape surface"), FTDFluidTerrainCapture::IsLandscapeSurfaceComponent(*MeshComponent));
		TestEqual(TEXT("The 2D driver delegates the landscape check for meshes"), UTDFluidVolumeDriverComponent::IsLandscapeSurfaceComponent(*MeshComponent), false);
	}
	TestEqual(TEXT("The 2D driver delegates the landscape check for landscapes"), UTDFluidVolumeDriverComponent::IsLandscapeSurfaceComponent(*LandscapeComponent), true);
	TestTrue(TEXT("Terrain only with a game viewport renders in the main renderer"), FTDFluidTerrainCapture::ShouldRenderInMainRenderer(ETDFluidHeightCaptureContent::TerrainOnly, true));
	TestFalse(TEXT("All geometry never renders in the main renderer"), FTDFluidTerrainCapture::ShouldRenderInMainRenderer(ETDFluidHeightCaptureContent::AllGeometry, true));
	TestEqual(TEXT("The 2D driver delegates the main renderer rule"), UTDFluidVolumeDriverComponent::ShouldRenderHeightCaptureInMainRenderer(ETDFluidHeightCaptureContent::TerrainOnly, false), FTDFluidTerrainCapture::ShouldRenderInMainRenderer(ETDFluidHeightCaptureContent::TerrainOnly, false));
	return true;
}

#endif
