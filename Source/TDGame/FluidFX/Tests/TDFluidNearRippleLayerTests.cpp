#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidNearRippleLayer.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNearRippleWindowTorusMathTest,
	"TDGame.FluidFX.NearRipple.WindowTorusMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNearRippleWindowTorusMathTest::RunTest(const FString& Parameters)
{
	constexpr int32 Resolution = 256;
	constexpr double CellSizeCm = 600.0 / Resolution;
	FRandomStream Random(7);
	for (int32 SampleIndex = 0; SampleIndex < 64; ++SampleIndex)
	{
		const FVector2D Center(Random.FRandRange(-5.0e6f, 5.0e6f), Random.FRandRange(-5.0e6f, 5.0e6f));
		const FIntPoint WindowMinCell = FTDFluidNearRippleLayer::ComputeWindowMinCell(Center, CellSizeCm, Resolution);
		const FIntPoint CenterCell(FMath::FloorToInt32(Center.X / CellSizeCm), FMath::FloorToInt32(Center.Y / CellSizeCm));
		TestTrue(TEXT("Window starts half a window before the centre cell"), WindowMinCell == CenterCell - FIntPoint(Resolution / 2));

		TSet<int32> StorageIndices;
		for (int32 Offset = 0; Offset < Resolution; ++Offset)
		{
			const FIntPoint WorldCell = WindowMinCell + FIntPoint(Offset, Offset);
			const FIntPoint StorageCell = FTDFluidNearRippleLayer::ComputeStorageCell(WorldCell, Resolution);
			StorageIndices.Add(StorageCell.X);
			TestTrue(TEXT("Window cell is inside its own window"), FTDFluidNearRippleLayer::IsCellInsideWindow(WorldCell, WindowMinCell, Resolution));
		}
		TestEqual(TEXT("Storage mapping is one to one across a window"), StorageIndices.Num(), Resolution);

		const int32 ShiftCells = Random.RandRange(-Resolution - 8, Resolution + 8);
		const FIntPoint ShiftedMinCell = WindowMinCell + FIntPoint(ShiftCells, 0);
		int32 IncomingColumnCount = 0;
		for (int32 Offset = 0; Offset < Resolution; ++Offset)
		{
			if (!FTDFluidNearRippleLayer::IsCellInsideWindow(ShiftedMinCell + FIntPoint(Offset, 0), WindowMinCell, Resolution))
			{
				++IncomingColumnCount;
			}
		}
		TestEqual(TEXT("Incoming columns equal the shift"), IncomingColumnCount, FMath::Min(FMath::Abs(ShiftCells), Resolution));
		TestEqual(TEXT("A shift of a full window resets the state"), FTDFluidNearRippleLayer::ShouldResetForShift(WindowMinCell, ShiftedMinCell, Resolution), FMath::Abs(ShiftCells) >= Resolution);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNearRippleSubstepCountAndClampTest,
	"TDGame.FluidFX.NearRipple.SubstepCountAndClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNearRippleSubstepCountAndClampTest::RunTest(const FString& Parameters)
{
	constexpr float CellSizeCm = 600.0f / 256.0f;
	const FTDFluidNearRippleSubstepPlan SixtyHertz = FTDFluidNearRippleLayer::ComputeSubstepPlan(40.0f, 1.0f / 60.0f, CellSizeCm, 4);
	TestEqual(TEXT("60 Hz at 40 cm/s needs one substep"), SixtyHertz.SubstepCount, 1);
	TestEqual(TEXT("60 Hz substep covers the frame"), SixtyHertz.SubstepDeltaSeconds, 1.0f / 60.0f);

	const FTDFluidNearRippleSubstepPlan ThirtyHertz = FTDFluidNearRippleLayer::ComputeSubstepPlan(40.0f, 1.0f / 30.0f, CellSizeCm, 4);
	TestEqual(TEXT("30 Hz at 40 cm/s needs two substeps"), ThirtyHertz.SubstepCount, 2);

	const FTDFluidNearRippleSubstepPlan Clamped = FTDFluidNearRippleLayer::ComputeSubstepPlan(80.0f, 0.1f, CellSizeCm, 4);
	const float StableSubstepSeconds = 0.5f * CellSizeCm / (80.0f * UE_SQRT_2);
	TestEqual(TEXT("Fast waves stop at the substep limit"), Clamped.SubstepCount, 4);
	TestTrue(TEXT("Clamped substep keeps the stability bound"), FMath::IsNearlyEqual(Clamped.SubstepDeltaSeconds, StableSubstepSeconds, 1.0e-7f));

	const double Phase = FTDFluidNearRippleLayer::ComputeIdleContactPhase(1.0e6 + 0.25, 1.0f);
	TestTrue(TEXT("Idle contact phase wraps in double precision"), FMath::IsNearlyEqual(Phase, UE_DOUBLE_HALF_PI, 1.0e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNearRippleConsoleStatesTest,
	"TDGame.FluidFX.NearRipple.ConsoleStatesAndDisabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNearRippleConsoleStatesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Preset state is followed by default"), FTDFluidNearRippleLayer::ResolveEnabled(-1, true));
	TestFalse(TEXT("Disabled preset stays off by default"), FTDFluidNearRippleLayer::ResolveEnabled(-1, false));
	TestFalse(TEXT("Console zero forces off"), FTDFluidNearRippleLayer::ResolveEnabled(0, true));
	TestTrue(TEXT("Console one forces on"), FTDFluidNearRippleLayer::ResolveEnabled(1, false));

	FTDFluidNearRippleLayer Layer;
	FTDFluidWaterNearRippleSettings Settings;
	Layer.Configure(Settings);
	TestTrue(TEXT("A new layer starts with a reset"), Layer.NeedsReset());
	Settings.Resolution = 128;
	Layer.Configure(Settings);
	TestEqual(TEXT("Resolution follows the settings"), Layer.GetResolution(), 128);
	TestTrue(TEXT("A changed grid asks for a reset"), Layer.NeedsReset());
	TestEqual(TEXT("Resolution rounds down to the alignment"), FTDFluidNearRippleLayer::SanitizeResolution(300), 296);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidNearRippleSourceCollectionTest,
	"TDGame.FluidFX.NearRipple.SourceCollection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidNearRippleSourceCollectionTest::RunTest(const FString& Parameters)
{
	FTDFluidWaterNearRippleSettings Settings;
	Settings.MaxSourcesPerFrame = 3;
	FTDFluidNearRippleLayer Layer;
	Layer.Configure(Settings);
	Layer.AddMovingSource(FVector2D(200.0, 0.0), FVector2D(150.0, 0.0), 45.0f);
	Layer.AddIdleContact(FVector2D(10.0, 0.0));
	Layer.AddMovingSource(FVector2D(-10.0, 0.0), FVector2D(75.0, 0.0), 45.0f);
	Layer.AddMovingSource(FVector2D(0.0, 10.0), FVector2D(300.0, 0.0), 45.0f);

	const TArray<FTDFluidNearRippleSourceSelection> Selected = Layer.SelectFrameSources(FVector2D::ZeroVector);
	if (!TestEqual(TEXT("Sources stop at the frame limit"), Selected.Num(), 3))
	{
		return false;
	}

	TestTrue(TEXT("Equal distances keep the input order"), Selected[0].WorldXY == FVector2D(10.0, 0.0));
	TestTrue(TEXT("Second nearest keeps the input order"), Selected[1].WorldXY == FVector2D(-10.0, 0.0));
	TestTrue(TEXT("Farthest source is dropped"), Selected[2].WorldXY == FVector2D(0.0, 10.0));
	TestEqual(TEXT("Idle contact carries no impulse"), Selected[0].ImpulseCmPerSec, 0.0f);
	TestEqual(TEXT("Idle contact is fully idle"), Selected[0].IdleWeight, 1.0f);
	TestEqual(TEXT("Moving source saturates at the reference speed"), Selected[2].ImpulseCmPerSec, Settings.MovingImpulseCmPerSec);
	TestTrue(TEXT("Slow moving source is weaker"), Selected[1].ImpulseCmPerSec < Settings.MovingImpulseCmPerSec);
	TestEqual(TEXT("Moving source radius follows the stamp scale"), Selected[1].RadiusCm, 45.0f * Settings.SourceRadiusScale);
	return true;
}

#endif
