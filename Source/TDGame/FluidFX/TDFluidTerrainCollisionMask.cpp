#include "FluidFX/TDFluidTerrainCollisionMask.h"

void FTDFluidTerrainCollisionMask::Reset(int32 InResolution)
{
	Resolution = FMath::Max(InResolution, 0);
	CellElevationsCm.Reset();
	CellElevationsCm.SetNumZeroed(Resolution * Resolution);
	KnownCells.Reset();
	KnownCells.SetNumZeroed(Resolution * Resolution);
}

void FTDFluidTerrainCollisionMask::SetCellElevation(int32 CellX, int32 CellY, float ElevationCm)
{
	if (CellX < 0 || CellY < 0 || CellX >= Resolution || CellY >= Resolution)
	{
		return;
	}

	const int32 Index = CellY * Resolution + CellX;
	CellElevationsCm[Index] = ElevationCm;
	KnownCells[Index] = 1;
}

void FTDFluidTerrainCollisionMask::Shift(const FIntPoint& ShiftCells)
{
	if (Resolution <= 0 || ShiftCells == FIntPoint::ZeroValue)
	{
		return;
	}

	TArray<float> ShiftedElevations;
	ShiftedElevations.SetNumZeroed(Resolution * Resolution);
	TArray<uint8> ShiftedKnown;
	ShiftedKnown.SetNumZeroed(Resolution * Resolution);
	for (int32 CellY = 0; CellY < Resolution; ++CellY)
	{
		const int32 SourceY = CellY + ShiftCells.Y;
		if (SourceY < 0 || SourceY >= Resolution)
		{
			continue;
		}
		for (int32 CellX = 0; CellX < Resolution; ++CellX)
		{
			const int32 SourceX = CellX + ShiftCells.X;
			if (SourceX < 0 || SourceX >= Resolution)
			{
				continue;
			}
			const int32 SourceIndex = SourceY * Resolution + SourceX;
			const int32 TargetIndex = CellY * Resolution + CellX;
			ShiftedElevations[TargetIndex] = CellElevationsCm[SourceIndex];
			ShiftedKnown[TargetIndex] = KnownCells[SourceIndex];
		}
	}
	CellElevationsCm = MoveTemp(ShiftedElevations);
	KnownCells = MoveTemp(ShiftedKnown);
}

bool FTDFluidTerrainCollisionMask::IsCellSolid(int32 CellX, int32 CellY, float BaseZCm, float AboveCm) const
{
	if (CellX < 0 || CellY < 0 || CellX >= Resolution || CellY >= Resolution)
	{
		return false;
	}

	const int32 Index = CellY * Resolution + CellX;
	return KnownCells[Index] != 0 && CellElevationsCm[Index] - BaseZCm > AboveCm;
}

bool FTDFluidTerrainCollisionMask::IsBlockSolid(int32 BlockX, int32 BlockY, int32 BlockScale, float BaseZCm, float AboveCm) const
{
	const int32 EndY = FMath::Min((BlockY + 1) * BlockScale, Resolution);
	const int32 EndX = FMath::Min((BlockX + 1) * BlockScale, Resolution);
	for (int32 CellY = BlockY * BlockScale; CellY < EndY; ++CellY)
	{
		for (int32 CellX = BlockX * BlockScale; CellX < EndX; ++CellX)
		{
			if (IsCellSolid(CellX, CellY, BaseZCm, AboveCm))
			{
				return true;
			}
		}
	}
	return false;
}

void FTDFluidTerrainCollisionMask::CoverSolidBlocks(int32 BlockScale, float BaseZCm, float AboveCm, TArray<FTDFluidCellRect>& OutRects) const
{
	const int32 BlockCount = FMath::DivideAndRoundUp(Resolution, BlockScale);
	TArray<int32> OpenRectIndices;
	TArray<int32> NextOpenRectIndices;
	for (int32 BlockY = 0; BlockY < BlockCount; ++BlockY)
	{
		NextOpenRectIndices.Reset();
		int32 BlockX = 0;
		while (BlockX < BlockCount)
		{
			if (!IsBlockSolid(BlockX, BlockY, BlockScale, BaseZCm, AboveCm))
			{
				++BlockX;
				continue;
			}

			const int32 RunBegin = BlockX;
			while (BlockX < BlockCount && IsBlockSolid(BlockX, BlockY, BlockScale, BaseZCm, AboveCm))
			{
				++BlockX;
			}

			const int32* ContinuedRect = OpenRectIndices.FindByPredicate([&OutRects, RunBegin, BlockX](int32 RectIndex)
			{
				return OutRects[RectIndex].MinX == RunBegin && OutRects[RectIndex].EndX == BlockX;
			});
			if (ContinuedRect)
			{
				OutRects[*ContinuedRect].EndY = BlockY + 1;
				NextOpenRectIndices.Add(*ContinuedRect);
				continue;
			}

			FTDFluidCellRect& NewRect = OutRects.AddDefaulted_GetRef();
			NewRect.MinX = RunBegin;
			NewRect.EndX = BlockX;
			NewRect.MinY = BlockY;
			NewRect.EndY = BlockY + 1;
			NextOpenRectIndices.Add(OutRects.Num() - 1);
		}
		Swap(OpenRectIndices, NextOpenRectIndices);
	}

	OutRects.StableSort([](const FTDFluidCellRect& Left, const FTDFluidCellRect& Right)
	{
		return Left.MinY != Right.MinY ? Left.MinY < Right.MinY : Left.MinX < Right.MinX;
	});
}

int32 FTDFluidTerrainCollisionMask::BuildBoxes(float BaseZCm, float AboveCm, int32 MaxBoxes, TFunctionRef<FVector2D(int32, int32)> CellToWorldCenter, const FVector2D& CellSizeCm, TArray<FTDFluidSdfObstacle>& OutBoxes) const
{
	OutBoxes.Reset();
	if (Resolution <= 0)
	{
		return 1;
	}

	const int32 BoxLimit = FMath::Max(MaxBoxes, 1);
	int32 BlockScale = 1;
	TArray<FTDFluidCellRect> Rects;
	for (;;)
	{
		Rects.Reset();
		CoverSolidBlocks(BlockScale, BaseZCm, AboveCm, Rects);
		if (Rects.Num() <= BoxLimit || BlockScale >= Resolution)
		{
			break;
		}
		BlockScale *= 2;
	}

	OutBoxes.Reserve(Rects.Num());
	for (const FTDFluidCellRect& Rect : Rects)
	{
		const int32 FirstCellX = Rect.MinX * BlockScale;
		const int32 FirstCellY = Rect.MinY * BlockScale;
		const int32 LastCellX = FMath::Min(Rect.EndX * BlockScale, Resolution) - 1;
		const int32 LastCellY = FMath::Min(Rect.EndY * BlockScale, Resolution) - 1;
		const FVector2D CenterCm = 0.5 * (CellToWorldCenter(FirstCellX, FirstCellY) + CellToWorldCenter(LastCellX, LastCellY));

		FTDFluidSdfObstacle& Box = OutBoxes.AddDefaulted_GetRef();
		Box.bIsBox = true;
		Box.WorldCenter = FVector(CenterCm, BaseZCm);
		Box.BoxHalfExtentsCm = FVector2D((LastCellX - FirstCellX + 1) * CellSizeCm.X, (LastCellY - FirstCellY + 1) * CellSizeCm.Y) * 0.5;
		Box.RadiusCm = static_cast<float>(Box.BoxHalfExtentsCm.Size());
	}
	return BlockScale;
}
