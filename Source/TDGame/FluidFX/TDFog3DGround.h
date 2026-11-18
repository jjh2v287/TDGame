#pragma once

#include "CoreMinimal.h"

struct TDGAME_API FTDFog3DGroundRect
{
	FIntPoint MinCell = FIntPoint::ZeroValue;
	int32 Size = 0;

	bool IsValid() const { return Size > 0; }
	bool CoversWindowWithRing(const FIntPoint& WindowMinCell, int32 WindowCells) const;

	bool operator==(const FTDFog3DGroundRect& Other) const
	{
		return MinCell == Other.MinCell && Size == Other.Size;
	}

	bool operator!=(const FTDFog3DGroundRect& Other) const
	{
		return !(*this == Other);
	}
};

struct FTDFog3DGroundCapturePlacement
{
	FTDFog3DGroundRect Rect;
	FIntPoint CenterCell = FIntPoint::ZeroValue;
	FVector CaptureWorld = FVector::ZeroVector;
	double OrthoWidthCm = 0.0;
	float DecodeOffsetCm = 0.0f;
};

struct TDGAME_API FTDFog3DGroundLayout
{
	static int32 ComputeCaptureSize(int32 WindowCells, int32 BorderCells);
	static FIntPoint ComputeCaptureCenterCell(const FIntPoint& WindowMinCell, int32 WindowCells);
	static FTDFog3DGroundRect ComputeCaptureRect(const FIntPoint& WindowMinCell, int32 WindowCells, int32 BorderCells);
	static FTDFog3DGroundCapturePlacement ComputeCapturePlacement(
		const FIntPoint& WindowMinCell,
		int32 WindowCells,
		int32 BorderCells,
		double CellSizeCm,
		double FloorZ,
		double ZoneHalfHeightCm,
		double HeightAboveZoneCm);
	static FVector4f ComputeMaterialGround(const FTDFog3DGroundRect& Rect, double CellSizeCm, const FVector& ZoneReferenceWorld);
};

enum class ETDFog3DGroundTrackerState : uint8
{
	NoImage,
	Requested,
	Ready
};

struct FTDFog3DGroundDecision
{
	bool bRequestsCapture = false;
	bool bConvertsCapture = false;
	bool bAllowsStep = false;
	FTDFog3DGroundRect StepRect;
};

class TDGAME_API FTDFog3DGroundTracker
{
public:
	void Reset();
	FTDFog3DGroundDecision Advance(const FTDFog3DGroundRect& DesiredRect, const FIntPoint& NextWindowMinCell, int32 WindowCells, int32 LatencyFrames, uint64 FrameNumber, bool bWasSceneDrawnSinceLastAdvance);
	void ConfirmStep(const FTDFog3DGroundDecision& Decision);
	bool RequestReconversion();

	ETDFog3DGroundTrackerState GetState() const { return State; }
	const FTDFog3DGroundRect& GetRequestRect() const { return RequestRect; }
	uint64 GetRequestFrame() const { return RequestFrame; }
	int32 GetDrawnFramesSinceRequest() const { return DrawnFramesSinceRequest; }
	const FTDFog3DGroundRect& GetConvertedRect() const { return ConvertedRect; }

private:
	bool IsRequestReady(int32 LatencyFrames) const;

	ETDFog3DGroundTrackerState State = ETDFog3DGroundTrackerState::NoImage;
	FTDFog3DGroundRect RequestRect;
	uint64 RequestFrame = 0;
	int32 DrawnFramesSinceRequest = 0;
	bool bIsRequestConverted = false;
	FTDFog3DGroundRect ConvertedRect;
};
