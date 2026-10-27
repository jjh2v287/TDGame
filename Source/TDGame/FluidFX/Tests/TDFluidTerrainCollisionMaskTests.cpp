#if WITH_DEV_AUTOMATION_TESTS

#include "FluidFX/TDFluidTerrainCollisionMask.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

namespace TDFluidCollisionMaskTesting
{
	constexpr int32 MaskResolution = 32;
	constexpr double MaskCellCm = 10.0;
	constexpr float MaskBaseZCm = 0.0f;
	constexpr float MaskAboveCm = 300.0f;

	FVector2D MaskCellToWorld(int32 CellX, int32 CellY)
	{
		return FVector2D((CellX + 0.5) * MaskCellCm, (CellY + 0.5) * MaskCellCm);
	}

	void FillRandomMask(FTDFluidTerrainCollisionMask& Mask, int32 Seed)
	{
		FRandomStream Stream(Seed);
		Mask.Reset(MaskResolution);
		for (int32 CellY = 0; CellY < MaskResolution; ++CellY)
		{
			for (int32 CellX = 0; CellX < MaskResolution; ++CellX)
			{
				const float Roll = Stream.FRand();
				if (Roll < 0.05f)
				{
					continue;
				}
				Mask.SetCellElevation(CellX, CellY, Roll < 0.35f ? 500.0f : 100.0f);
			}
		}
	}

	TArray<int32> CountCoverage(const TArray<FTDFluidSdfObstacle>& Boxes)
	{
		TArray<int32> Coverage;
		Coverage.SetNumZeroed(MaskResolution * MaskResolution);
		for (const FTDFluidSdfObstacle& Box : Boxes)
		{
			const int32 FirstX = FMath::RoundToInt32((Box.WorldCenter.X - Box.BoxHalfExtentsCm.X) / MaskCellCm);
			const int32 EndX = FMath::RoundToInt32((Box.WorldCenter.X + Box.BoxHalfExtentsCm.X) / MaskCellCm);
			const int32 FirstY = FMath::RoundToInt32((Box.WorldCenter.Y - Box.BoxHalfExtentsCm.Y) / MaskCellCm);
			const int32 EndY = FMath::RoundToInt32((Box.WorldCenter.Y + Box.BoxHalfExtentsCm.Y) / MaskCellCm);
			for (int32 CellY = FMath::Max(FirstY, 0); CellY < FMath::Min(EndY, MaskResolution); ++CellY)
			{
				for (int32 CellX = FMath::Max(FirstX, 0); CellX < FMath::Min(EndX, MaskResolution); ++CellX)
				{
					++Coverage[CellY * MaskResolution + CellX];
				}
			}
		}
		return Coverage;
	}

	bool HaveSameBoxes(const TArray<FTDFluidSdfObstacle>& Left, const TArray<FTDFluidSdfObstacle>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (Left[Index].WorldCenter != Right[Index].WorldCenter || Left[Index].BoxHalfExtentsCm != Right[Index].BoxHalfExtentsCm)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidTerrainCollisionMaskBoxesTest,
	"TDGame.FluidFX.Terrain.CollisionMaskBoxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidTerrainCollisionMaskBoxesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidCollisionMaskTesting;

	FTDFluidTerrainCollisionMask EmptyMask;
	EmptyMask.Reset(MaskResolution);
	TArray<FTDFluidSdfObstacle> EmptyBoxes;
	TestEqual(TEXT("An unknown mask has block scale 1"), EmptyMask.BuildBoxes(MaskBaseZCm, MaskAboveCm, 16, [](int32 CellX, int32 CellY) { return MaskCellToWorld(CellX, CellY); }, FVector2D(MaskCellCm), EmptyBoxes), 1);
	TestEqual(TEXT("An unknown mask builds no boxes"), EmptyBoxes.Num(), 0);

	FTDFluidTerrainCollisionMask Mask;
	FillRandomMask(Mask, 20261026);
	TArray<FTDFluidSdfObstacle> Boxes;
	const int32 FineScale = Mask.BuildBoxes(MaskBaseZCm, MaskAboveCm, 4096, [](int32 CellX, int32 CellY) { return MaskCellToWorld(CellX, CellY); }, FVector2D(MaskCellCm), Boxes);
	TestEqual(TEXT("A generous limit keeps block scale 1"), FineScale, 1);
	TestTrue(TEXT("The random mask has solid cells"), Boxes.Num() > 0);

	const TArray<int32> Coverage = CountCoverage(Boxes);
	int32 MismatchCount = 0;
	for (int32 CellY = 0; CellY < MaskResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < MaskResolution; ++CellX)
		{
			const int32 Expected = Mask.IsCellSolid(CellX, CellY, MaskBaseZCm, MaskAboveCm) ? 1 : 0;
			MismatchCount += Coverage[CellY * MaskResolution + CellX] != Expected ? 1 : 0;
		}
	}
	TestEqual(TEXT("Box union equals the solid mask with no overlap"), MismatchCount, 0);

	bool bIsSorted = true;
	for (int32 Index = 1; Index < Boxes.Num(); ++Index)
	{
		const FVector& Previous = Boxes[Index - 1].WorldCenter - FVector(Boxes[Index - 1].BoxHalfExtentsCm, 0.0);
		const FVector& Current = Boxes[Index].WorldCenter - FVector(Boxes[Index].BoxHalfExtentsCm, 0.0);
		bIsSorted &= Previous.Y < Current.Y - 1e-3 || (FMath::IsNearlyEqual(Previous.Y, Current.Y, 1e-3) && Previous.X < Current.X);
	}
	TestTrue(TEXT("Boxes are ordered by first row then first column"), bIsSorted);
	for (const FTDFluidSdfObstacle& Box : Boxes)
	{
		TestTrue(TEXT("Every terrain obstacle is a box"), Box.bIsBox);
	}

	TArray<FTDFluidSdfObstacle> RepeatedBoxes;
	Mask.BuildBoxes(MaskBaseZCm, MaskAboveCm, 4096, [](int32 CellX, int32 CellY) { return MaskCellToWorld(CellX, CellY); }, FVector2D(MaskCellCm), RepeatedBoxes);
	TestTrue(TEXT("Building twice gives identical boxes"), HaveSameBoxes(Boxes, RepeatedBoxes));

	TArray<FTDFluidSdfObstacle> CoarseBoxes;
	const int32 CoarseLimit = FMath::Max(Boxes.Num() / 4, 1);
	const int32 CoarseScale = Mask.BuildBoxes(MaskBaseZCm, MaskAboveCm, CoarseLimit, [](int32 CellX, int32 CellY) { return MaskCellToWorld(CellX, CellY); }, FVector2D(MaskCellCm), CoarseBoxes);
	TestTrue(*FString::Printf(TEXT("Exceeding the limit coarsens the blocks (scale %d)"), CoarseScale), CoarseScale >= 2);
	TestTrue(*FString::Printf(TEXT("Coarse boxes respect the limit (%d <= %d)"), CoarseBoxes.Num(), CoarseLimit), CoarseBoxes.Num() <= CoarseLimit || CoarseScale >= MaskResolution);
	const TArray<int32> CoarseCoverage = CountCoverage(CoarseBoxes);
	int32 UncoveredSolidCount = 0;
	int32 OverlapCount = 0;
	for (int32 CellY = 0; CellY < MaskResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < MaskResolution; ++CellX)
		{
			const int32 Covered = CoarseCoverage[CellY * MaskResolution + CellX];
			UncoveredSolidCount += Mask.IsCellSolid(CellX, CellY, MaskBaseZCm, MaskAboveCm) && Covered == 0 ? 1 : 0;
			OverlapCount += Covered > 1 ? 1 : 0;
		}
	}
	TestEqual(TEXT("Coarse boxes still cover every solid cell"), UncoveredSolidCount, 0);
	TestEqual(TEXT("Coarse boxes do not overlap"), OverlapCount, 0);

	FTDFluidTerrainCollisionMask ShiftMask;
	ShiftMask.Reset(4);
	ShiftMask.SetCellElevation(1, 1, 500.0f);
	ShiftMask.SetCellElevation(3, 3, 500.0f);
	ShiftMask.SetCellElevation(2, 2, 100.0f);
	ShiftMask.Shift(FIntPoint(1, 1));
	TestTrue(TEXT("Shift moves a solid cell toward the origin"), ShiftMask.IsCellSolid(0, 0, MaskBaseZCm, MaskAboveCm));
	TestTrue(TEXT("Shift moves the far solid cell"), ShiftMask.IsCellSolid(2, 2, MaskBaseZCm, MaskAboveCm));
	TestFalse(TEXT("Shift carries low cells as not solid"), ShiftMask.IsCellSolid(1, 1, MaskBaseZCm, MaskAboveCm));
	TestFalse(TEXT("Incoming cells are unknown after a shift"), ShiftMask.IsCellSolid(3, 3, MaskBaseZCm, MaskAboveCm));
	return true;
}

#endif
