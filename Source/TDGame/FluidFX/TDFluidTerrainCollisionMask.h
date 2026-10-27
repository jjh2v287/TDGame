#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"

struct TDGAME_API FTDFluidTerrainCollisionMask
{
	void Reset(int32 InResolution);
	void SetCellElevation(int32 CellX, int32 CellY, float ElevationCm);
	void Shift(const FIntPoint& ShiftCells);
	bool IsCellSolid(int32 CellX, int32 CellY, float BaseZCm, float AboveCm) const;
	int32 BuildBoxes(float BaseZCm, float AboveCm, int32 MaxBoxes, TFunctionRef<FVector2D(int32, int32)> CellToWorldCenter, const FVector2D& CellSizeCm, TArray<FTDFluidSdfObstacle>& OutBoxes) const;
	int32 GetResolution() const { return Resolution; }

private:
	struct FTDFluidCellRect
	{
		int32 MinX = 0;
		int32 EndX = 0;
		int32 MinY = 0;
		int32 EndY = 0;
	};

	bool IsBlockSolid(int32 BlockX, int32 BlockY, int32 BlockScale, float BaseZCm, float AboveCm) const;
	void CoverSolidBlocks(int32 BlockScale, float BaseZCm, float AboveCm, TArray<FTDFluidCellRect>& OutRects) const;

	TArray<float> CellElevationsCm;
	TArray<uint8> KnownCells;
	int32 Resolution = 0;
};
