#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidPlaneTypes.h"

struct TDGAME_API FTDFluidPlaneBasis
{
	ETDFluidPlaneOrientation Orientation = ETDFluidPlaneOrientation::WorldFacing;
	FVector U = FVector(1.0, 0.0, 0.0);
	FVector V = FVector(0.0, 1.0, 0.0);
	FVector N = FVector(0.0, 0.0, 1.0);

	static FTDFluidPlaneBasis MakeWorldFacing();
	static FTDFluidPlaneBasis MakeFromView(ETDFluidPlaneOrientation InOrientation, const FRotator& ViewRotation, float LeanBackDegrees);
	static FRotator GetDefaultViewRotation();

	bool IsWorldFacing() const { return Orientation == ETDFluidPlaneOrientation::WorldFacing; }

	FVector2D ProjectPoint(const FVector& WorldPoint) const;
	FVector2D ProjectVector(const FVector& WorldVector) const;
	double ProjectDepth(const FVector& WorldPoint) const;
	FVector UnprojectPoint(const FVector2D& PlanePoint, double DepthCm) const;
	FVector UnprojectVector(const FVector2D& PlaneVector) const;
	bool IntersectRay(const FVector& RayOrigin, const FVector& RayDirection, double PlaneDepthCm, FVector& OutPoint) const;
	FQuat GetRotation() const;
	FVector2D GetPlaneUpDirection() const;
	bool IsNearlyEqual(const FTDFluidPlaneBasis& Other, double ToleranceDegrees = 1.0) const;
};
