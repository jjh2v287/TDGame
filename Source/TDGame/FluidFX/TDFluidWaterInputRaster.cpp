#include "FluidFX/TDFluidWaterInputRaster.h"

namespace
{
	float WaterInputRasterBankFalloffWeight(float DistanceCm, float HalfWidthCm, float FalloffCm)
	{
		if (DistanceCm >= HalfWidthCm)
		{
			return 0.0f;
		}

		const float EffectiveFalloffCm = FMath::Min(FalloffCm, HalfWidthCm);
		if (EffectiveFalloffCm <= 0.0f || DistanceCm <= HalfWidthCm - EffectiveFalloffCm)
		{
			return 1.0f;
		}

		const float Fraction = (DistanceCm - (HalfWidthCm - EffectiveFalloffCm)) / EffectiveFalloffCm;
		return 1.0f - Fraction * Fraction * (3.0f - 2.0f * Fraction);
	}
}

void FTDFluidWaterInputRaster::SetFlowGuides(const TArray<FTDFluidFlowGuide>& Guides)
{
	WaterGuideSegments.Reset();
	for (int32 GuideIndex = 0; GuideIndex < Guides.Num(); ++GuideIndex)
	{
		const TArray<FTDFluidFlowGuidePoint>& Points = Guides[GuideIndex].Points;
		for (int32 PointIndex = 0; PointIndex + 1 < Points.Num(); ++PointIndex)
		{
			const FTDFluidFlowGuidePoint& StartPoint = Points[PointIndex];
			const FTDFluidFlowGuidePoint& EndPoint = Points[PointIndex + 1];
			const bool bIsFinite = !StartPoint.WorldLocation.ContainsNaN() && !EndPoint.WorldLocation.ContainsNaN()
				&& FMath::IsFinite(StartPoint.HalfWidthCm) && FMath::IsFinite(EndPoint.HalfWidthCm)
				&& FMath::IsFinite(StartPoint.FlowSpeedCmPerSec) && FMath::IsFinite(EndPoint.FlowSpeedCmPerSec);
			if (!bIsFinite)
			{
				continue;
			}

			const FVector2D Start(StartPoint.WorldLocation.X, StartPoint.WorldLocation.Y);
			const FVector2D Offset(EndPoint.WorldLocation.X - Start.X, EndPoint.WorldLocation.Y - Start.Y);
			const double Length = Offset.Size();
			if (Length < UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			FTDFluidWaterGuideSegment Segment;
			Segment.Start = Start;
			Segment.Direction = Offset / Length;
			Segment.LengthCm = Length;
			Segment.StartSurfaceCm = static_cast<float>(StartPoint.WorldLocation.Z);
			Segment.EndSurfaceCm = static_cast<float>(EndPoint.WorldLocation.Z);
			Segment.StartHalfWidthCm = FMath::Max(StartPoint.HalfWidthCm, 0.0f);
			Segment.EndHalfWidthCm = FMath::Max(EndPoint.HalfWidthCm, 0.0f);
			Segment.StartSpeedCmPerSec = StartPoint.FlowSpeedCmPerSec;
			Segment.EndSpeedCmPerSec = EndPoint.FlowSpeedCmPerSec;
			Segment.DescentSlope = FMath::Max(0.0f, static_cast<float>((StartPoint.WorldLocation.Z - EndPoint.WorldLocation.Z) / Length));
			Segment.GuideIndex = GuideIndex;

			const double PaddingCm = FMath::Max(Segment.StartHalfWidthCm, Segment.EndHalfWidthCm);
			const FVector2D End(EndPoint.WorldLocation.X, EndPoint.WorldLocation.Y);
			Segment.MinimumX = FMath::Min(Start.X, End.X) - PaddingCm;
			Segment.MaximumX = FMath::Max(Start.X, End.X) + PaddingCm;
			Segment.MinimumY = FMath::Min(Start.Y, End.Y) - PaddingCm;
			Segment.MaximumY = FMath::Max(Start.Y, End.Y) + PaddingCm;
			WaterGuideSegments.Add(Segment);
		}
	}
}

void FTDFluidWaterInputRaster::SetWaterRegions(const TArray<FTDFluidWaterRegion>& Regions)
{
	WaterRegions.Reset();
	for (const FTDFluidWaterRegion& Region : Regions)
	{
		if (Region.MinXY.ContainsNaN() || Region.MaxXY.ContainsNaN() || !FMath::IsFinite(Region.SurfaceHeightCm))
		{
			continue;
		}

		FTDFluidWaterRegion NormalizedRegion;
		NormalizedRegion.MinXY = FVector2D(FMath::Min(Region.MinXY.X, Region.MaxXY.X), FMath::Min(Region.MinXY.Y, Region.MaxXY.Y));
		NormalizedRegion.MaxXY = FVector2D(FMath::Max(Region.MinXY.X, Region.MaxXY.X), FMath::Max(Region.MinXY.Y, Region.MaxXY.Y));
		NormalizedRegion.SurfaceHeightCm = Region.SurfaceHeightCm;
		WaterRegions.Add(NormalizedRegion);
	}
}

void FTDFluidWaterInputRaster::BuildRowSegmentLists(TConstArrayView<double> RowWorldY, double RowSlackCm)
{
	const int32 GridSize = RowWorldY.Num();

	WaterRowSegmentOffsets.Init(0, GridSize + 1);
	for (const FTDFluidWaterGuideSegment& Segment : WaterGuideSegments)
	{
		for (int32 Y = 0; Y < GridSize; ++Y)
		{
			if (RowWorldY[Y] >= Segment.MinimumY - RowSlackCm && RowWorldY[Y] <= Segment.MaximumY + RowSlackCm)
			{
				++WaterRowSegmentOffsets[Y + 1];
			}
		}
	}

	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		WaterRowSegmentOffsets[Y + 1] += WaterRowSegmentOffsets[Y];
	}

	WaterRowSegmentIndices.SetNumUninitialized(WaterRowSegmentOffsets[GridSize]);
	TArray<int32> RowCursors;
	RowCursors.SetNumUninitialized(GridSize);
	for (int32 Y = 0; Y < GridSize; ++Y)
	{
		RowCursors[Y] = WaterRowSegmentOffsets[Y];
	}

	for (int32 SegmentIndex = 0; SegmentIndex < WaterGuideSegments.Num(); ++SegmentIndex)
	{
		const FTDFluidWaterGuideSegment& Segment = WaterGuideSegments[SegmentIndex];
		for (int32 Y = 0; Y < GridSize; ++Y)
		{
			if (RowWorldY[Y] >= Segment.MinimumY - RowSlackCm && RowWorldY[Y] <= Segment.MaximumY + RowSlackCm)
			{
				WaterRowSegmentIndices[RowCursors[Y]++] = SegmentIndex;
			}
		}
	}
}

void FTDFluidWaterInputRaster::ReleaseRowSegmentLists()
{
	WaterRowSegmentOffsets.Empty();
	WaterRowSegmentIndices.Empty();
}

FTDFluidWaterRasterCell FTDFluidWaterInputRaster::RasterizeCell(const FVector2D& CellWorld, int32 RowIndex, float BankFalloffCm, float BankSpeedRatio, float NoSurfaceHeightCm) const
{
	const bool bHasRowSegmentList = RowIndex >= 0 && RowIndex + 1 < WaterRowSegmentOffsets.Num();
	const int32* RowOffsets = WaterRowSegmentOffsets.GetData();
	const int32* RowSegmentIndices = WaterRowSegmentIndices.GetData();
	const FTDFluidWaterGuideSegment* Segments = WaterGuideSegments.GetData();
	const FTDFluidWaterRegion* Regions = WaterRegions.GetData();
	const int32 RegionCount = WaterRegions.Num();
	const int32 SegmentBegin = bHasRowSegmentList ? RowOffsets[RowIndex] : 0;
	const int32 SegmentEnd = bHasRowSegmentList ? RowOffsets[RowIndex + 1] : 0;
	FTDFluidWaterRasterCell RasterCell;

	double WeightSum = 0.0;
	double VelocitySumX = 0.0;
	double VelocitySumY = 0.0;
	float StrongestWeight = 0.0f;
	float StrongestSurface = 0.0f;
	float StrongestDescent = 0.0f;

	int32 CurrentGuideIndex = INDEX_NONE;
	const FTDFluidWaterGuideSegment* NearestSegment = nullptr;
	float NearestDistanceCm = TNumericLimits<float>::Max();
	float NearestFraction = 0.0f;

	const auto FlushGuide = [&]()
	{
		if (NearestSegment == nullptr)
		{
			return;
		}

		const float HalfWidthCm = FMath::Lerp(NearestSegment->StartHalfWidthCm, NearestSegment->EndHalfWidthCm, NearestFraction);
		const float Weight = WaterInputRasterBankFalloffWeight(NearestDistanceCm, HalfWidthCm, BankFalloffCm);
		if (Weight > 0.0f)
		{
			const float NormalizedDistance = FMath::Clamp(NearestDistanceCm / HalfWidthCm, 0.0f, 1.0f);
			const float ProfileScale = BankSpeedRatio + (1.0f - BankSpeedRatio) * (1.0f - NormalizedDistance * NormalizedDistance);
			const float Speed = FMath::Lerp(NearestSegment->StartSpeedCmPerSec, NearestSegment->EndSpeedCmPerSec, NearestFraction) * ProfileScale;
			WeightSum += Weight;
			VelocitySumX += Weight * NearestSegment->Direction.X * Speed;
			VelocitySumY += Weight * NearestSegment->Direction.Y * Speed;
			if (Weight > StrongestWeight)
			{
				StrongestWeight = Weight;
				StrongestSurface = FMath::Lerp(NearestSegment->StartSurfaceCm, NearestSegment->EndSurfaceCm, NearestFraction);
				StrongestDescent = NearestSegment->DescentSlope;
			}
		}

		NearestSegment = nullptr;
		NearestDistanceCm = TNumericLimits<float>::Max();
	};

	for (int32 ListIndex = SegmentBegin; ListIndex < SegmentEnd; ++ListIndex)
	{
		const FTDFluidWaterGuideSegment& Segment = Segments[RowSegmentIndices[ListIndex]];
		if (Segment.GuideIndex != CurrentGuideIndex)
		{
			FlushGuide();
			CurrentGuideIndex = Segment.GuideIndex;
		}

		if (CellWorld.X < Segment.MinimumX || CellWorld.X > Segment.MaximumX)
		{
			continue;
		}

		const double AlongCm = FMath::Clamp(FVector2D::DotProduct(CellWorld - Segment.Start, Segment.Direction), 0.0, Segment.LengthCm);
		const FVector2D ClosestPoint = Segment.Start + Segment.Direction * AlongCm;
		const float DistanceCm = static_cast<float>((CellWorld - ClosestPoint).Size());
		if (DistanceCm < NearestDistanceCm)
		{
			NearestSegment = &Segment;
			NearestDistanceCm = DistanceCm;
			NearestFraction = static_cast<float>(AlongCm / Segment.LengthCm);
		}
	}
	FlushGuide();

	if (WeightSum > 0.0)
	{
		RasterCell.SurfaceHeightCm = StrongestSurface;
		RasterCell.TargetVelocityX = static_cast<float>(VelocitySumX / WeightSum);
		RasterCell.TargetVelocityY = static_cast<float>(VelocitySumY / WeightSum);
		RasterCell.GuideWeight = StrongestWeight;
		RasterCell.GuideDescent = StrongestDescent;
	}
	else
	{
		float RegionSurface = NoSurfaceHeightCm;
		for (int32 RegionIndex = 0; RegionIndex < RegionCount; ++RegionIndex)
		{
			const FTDFluidWaterRegion& Region = Regions[RegionIndex];
			const bool bIsInsideRegion = CellWorld.X >= Region.MinXY.X && CellWorld.X <= Region.MaxXY.X
				&& CellWorld.Y >= Region.MinXY.Y && CellWorld.Y <= Region.MaxXY.Y;
			if (bIsInsideRegion)
			{
				RegionSurface = FMath::Max(RegionSurface, Region.SurfaceHeightCm);
			}
		}
		RasterCell.SurfaceHeightCm = RegionSurface;
		RasterCell.TargetVelocityX = 0.0f;
		RasterCell.TargetVelocityY = 0.0f;
		RasterCell.GuideWeight = 0.0f;
		RasterCell.GuideDescent = 0.0f;
	}

	return RasterCell;
}
