#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"

struct FTDFluidWaterGuideSegment
{
	FVector2D Start = FVector2D::ZeroVector;
	FVector2D Direction = FVector2D::ZeroVector;
	double LengthCm = 0.0;
	double MinimumX = 0.0;
	double MaximumX = 0.0;
	double MinimumY = 0.0;
	double MaximumY = 0.0;
	float StartSurfaceCm = 0.0f;
	float EndSurfaceCm = 0.0f;
	float StartHalfWidthCm = 0.0f;
	float EndHalfWidthCm = 0.0f;
	float StartSpeedCmPerSec = 0.0f;
	float EndSpeedCmPerSec = 0.0f;
	float DescentSlope = 0.0f;
	int32 GuideIndex = 0;
};

struct FTDFluidWaterRasterCell
{
	float SurfaceHeightCm = 0.0f;
	float TargetVelocityX = 0.0f;
	float TargetVelocityY = 0.0f;
	float GuideWeight = 0.0f;
	float GuideDescent = 0.0f;
};

class TDGAME_API FTDFluidWaterInputRaster
{
public:
	void SetFlowGuides(const TArray<FTDFluidFlowGuide>& Guides);
	void SetWaterRegions(const TArray<FTDFluidWaterRegion>& Regions);
	void BuildRowSegmentLists(TConstArrayView<double> RowWorldY, double RowSlackCm);
	void ReleaseRowSegmentLists();
	FTDFluidWaterRasterCell RasterizeCell(const FVector2D& CellWorld, int32 RowIndex, float BankFalloffCm, float BankSpeedRatio, float NoSurfaceHeightCm) const;

private:
	TArray<FTDFluidWaterGuideSegment> WaterGuideSegments;
	TArray<FTDFluidWaterRegion> WaterRegions;
	TArray<int32> WaterRowSegmentOffsets;
	TArray<int32> WaterRowSegmentIndices;
};
