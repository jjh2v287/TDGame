#include "FluidFX/TDFog3DGround.h"

bool FTDFog3DGroundRect::CoversWindowWithRing(const FIntPoint& WindowMinCell, int32 WindowCells) const
{
	if (!IsValid() || WindowCells <= 0)
	{
		return false;
	}
	const int64 RingMinX = static_cast<int64>(WindowMinCell.X) - 1;
	const int64 RingMinY = static_cast<int64>(WindowMinCell.Y) - 1;
	const int64 RingEndX = static_cast<int64>(WindowMinCell.X) + WindowCells + 1;
	const int64 RingEndY = static_cast<int64>(WindowMinCell.Y) + WindowCells + 1;
	return MinCell.X <= RingMinX && MinCell.Y <= RingMinY
		&& static_cast<int64>(MinCell.X) + Size >= RingEndX
		&& static_cast<int64>(MinCell.Y) + Size >= RingEndY;
}

int32 FTDFog3DGroundLayout::ComputeCaptureSize(int32 WindowCells, int32 BorderCells)
{
	if (WindowCells <= 0 || BorderCells < 0)
	{
		return 0;
	}
	return WindowCells + 2 * BorderCells;
}

FIntPoint FTDFog3DGroundLayout::ComputeCaptureCenterCell(const FIntPoint& WindowMinCell, int32 WindowCells)
{
	return WindowMinCell + FIntPoint(WindowCells / 2, WindowCells / 2);
}

FTDFog3DGroundRect FTDFog3DGroundLayout::ComputeCaptureRect(const FIntPoint& WindowMinCell, int32 WindowCells, int32 BorderCells)
{
	FTDFog3DGroundRect Rect;
	const int32 CaptureSize = ComputeCaptureSize(WindowCells, BorderCells);
	if (CaptureSize <= 0)
	{
		return Rect;
	}
	Rect.Size = CaptureSize;
	Rect.MinCell = ComputeCaptureCenterCell(WindowMinCell, WindowCells) - FIntPoint(CaptureSize / 2, CaptureSize / 2);
	return Rect;
}

FTDFog3DGroundCapturePlacement FTDFog3DGroundLayout::ComputeCapturePlacement(
	const FIntPoint& WindowMinCell,
	int32 WindowCells,
	int32 BorderCells,
	double CellSizeCm,
	double FloorZ,
	double ZoneHalfHeightCm,
	double HeightAboveZoneCm)
{
	FTDFog3DGroundCapturePlacement Placement;
	Placement.Rect = ComputeCaptureRect(WindowMinCell, WindowCells, BorderCells);
	Placement.CenterCell = ComputeCaptureCenterCell(WindowMinCell, WindowCells);
	const double CaptureZ = FloorZ + 2.0 * ZoneHalfHeightCm + HeightAboveZoneCm;
	Placement.CaptureWorld = FVector(static_cast<double>(Placement.CenterCell.X) * CellSizeCm, static_cast<double>(Placement.CenterCell.Y) * CellSizeCm, CaptureZ);
	Placement.OrthoWidthCm = static_cast<double>(Placement.Rect.Size) * CellSizeCm;
	Placement.DecodeOffsetCm = static_cast<float>(CaptureZ - FloorZ);
	return Placement;
}

FVector4f FTDFog3DGroundLayout::ComputeMaterialGround(const FTDFog3DGroundRect& Rect, double CellSizeCm, const FVector& ZoneReferenceWorld)
{
	return FVector4f(
		static_cast<float>(static_cast<double>(Rect.MinCell.X) * CellSizeCm - ZoneReferenceWorld.X),
		static_cast<float>(static_cast<double>(Rect.MinCell.Y) * CellSizeCm - ZoneReferenceWorld.Y),
		static_cast<float>(Rect.Size),
		static_cast<float>(Rect.Size));
}

void FTDFog3DGroundTracker::Reset()
{
	State = ETDFog3DGroundTrackerState::NoImage;
	RequestRect = FTDFog3DGroundRect();
	RequestFrame = 0;
	DrawnFramesSinceRequest = 0;
	bIsRequestConverted = false;
	ConvertedRect = FTDFog3DGroundRect();
}

FTDFog3DGroundDecision FTDFog3DGroundTracker::Advance(const FTDFog3DGroundRect& DesiredRect, const FIntPoint& NextWindowMinCell, int32 WindowCells, int32 LatencyFrames, uint64 FrameNumber, bool bWasSceneDrawnSinceLastAdvance)
{
	FTDFog3DGroundDecision Decision;
	if (!DesiredRect.IsValid())
	{
		return Decision;
	}

	if (State == ETDFog3DGroundTrackerState::NoImage || DesiredRect != RequestRect)
	{
		State = ETDFog3DGroundTrackerState::Requested;
		RequestRect = DesiredRect;
		RequestFrame = FrameNumber;
		DrawnFramesSinceRequest = 0;
		bIsRequestConverted = false;
		Decision.bRequestsCapture = true;
	}
	else if (State == ETDFog3DGroundTrackerState::Requested && bWasSceneDrawnSinceLastAdvance)
	{
		++DrawnFramesSinceRequest;
	}

	if (State == ETDFog3DGroundTrackerState::Requested && IsRequestReady(LatencyFrames))
	{
		State = ETDFog3DGroundTrackerState::Ready;
	}

	if (State == ETDFog3DGroundTrackerState::Ready && !bIsRequestConverted && RequestRect.CoversWindowWithRing(NextWindowMinCell, WindowCells))
	{
		Decision.bConvertsCapture = true;
		Decision.bAllowsStep = true;
		Decision.StepRect = RequestRect;
		return Decision;
	}

	if (ConvertedRect.CoversWindowWithRing(NextWindowMinCell, WindowCells))
	{
		Decision.bAllowsStep = true;
		Decision.StepRect = ConvertedRect;
	}
	return Decision;
}

void FTDFog3DGroundTracker::ConfirmStep(const FTDFog3DGroundDecision& Decision)
{
	if (!Decision.bConvertsCapture)
	{
		return;
	}
	ConvertedRect = Decision.StepRect;
	bIsRequestConverted = Decision.StepRect == RequestRect;
}

bool FTDFog3DGroundTracker::RequestReconversion()
{
	if (State != ETDFog3DGroundTrackerState::Ready || !bIsRequestConverted)
	{
		return false;
	}
	bIsRequestConverted = false;
	return true;
}

bool FTDFog3DGroundTracker::IsRequestReady(int32 LatencyFrames) const
{
	return DrawnFramesSinceRequest >= FMath::Max(LatencyFrames, 1);
}
