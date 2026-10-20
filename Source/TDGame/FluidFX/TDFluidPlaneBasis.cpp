#include "FluidFX/TDFluidPlaneBasis.h"

namespace
{
	constexpr double MaxLeanDegrees = 89.0;
	constexpr double MinRayPlaneAlignment = 1.0e-4;
	constexpr double DefaultViewPitchDegrees = -60.0;

	double ResolveYawDegrees(const FTDFluidPlaneBasis& Basis)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(-Basis.U.X, Basis.U.Y));
	}

	double ResolveLeanDegrees(const FTDFluidPlaneBasis& Basis)
	{
		return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(-Basis.N.Z, -1.0, 1.0)));
	}
}

FTDFluidPlaneBasis FTDFluidPlaneBasis::MakeWorldFacing()
{
	return FTDFluidPlaneBasis();
}

FTDFluidPlaneBasis FTDFluidPlaneBasis::MakeFromView(ETDFluidPlaneOrientation InOrientation, const FRotator& ViewRotation, float LeanBackDegrees)
{
	if (InOrientation == ETDFluidPlaneOrientation::WorldFacing)
	{
		return MakeWorldFacing();
	}

	const double LeanDegrees = InOrientation == ETDFluidPlaneOrientation::CameraFacing
		? FMath::Clamp(-static_cast<double>(ViewRotation.Pitch), 0.0, MaxLeanDegrees)
		: FMath::Clamp(static_cast<double>(LeanBackDegrees), 0.0, MaxLeanDegrees);

	double SinYaw = 0.0;
	double CosYaw = 1.0;
	double SinLean = 0.0;
	double CosLean = 1.0;
	FMath::SinCos(&SinYaw, &CosYaw, FMath::DegreesToRadians(static_cast<double>(ViewRotation.Yaw)));
	FMath::SinCos(&SinLean, &CosLean, FMath::DegreesToRadians(LeanDegrees));

	FTDFluidPlaneBasis Basis;
	Basis.Orientation = InOrientation;
	Basis.U = FVector(-SinYaw, CosYaw, 0.0);
	const FVector HorizontalForward(CosYaw, SinYaw, 0.0);
	Basis.V = FVector(0.0, 0.0, CosLean) + HorizontalForward * SinLean;
	Basis.N = FVector::CrossProduct(Basis.U, Basis.V);
	return Basis;
}

FRotator FTDFluidPlaneBasis::GetDefaultViewRotation()
{
	return FRotator(DefaultViewPitchDegrees, 0.0, 0.0);
}

FVector2D FTDFluidPlaneBasis::ProjectPoint(const FVector& WorldPoint) const
{
	if (IsWorldFacing())
	{
		return FVector2D(WorldPoint.X, WorldPoint.Y);
	}
	return ProjectVector(WorldPoint) + PlaneCoordinateOffsetCm;
}

FVector2D FTDFluidPlaneBasis::ProjectVector(const FVector& WorldVector) const
{
	if (IsWorldFacing())
	{
		return FVector2D(WorldVector.X, WorldVector.Y);
	}
	return FVector2D(FVector::DotProduct(WorldVector, U), FVector::DotProduct(WorldVector, V));
}

double FTDFluidPlaneBasis::ProjectDepth(const FVector& WorldPoint) const
{
	if (IsWorldFacing())
	{
		return WorldPoint.Z;
	}
	return FVector::DotProduct(WorldPoint, N);
}

FVector FTDFluidPlaneBasis::UnprojectPoint(const FVector2D& PlanePoint, double DepthCm) const
{
	if (IsWorldFacing())
	{
		return FVector(PlanePoint.X, PlanePoint.Y, DepthCm);
	}
	return UnprojectVector(PlanePoint - PlaneCoordinateOffsetCm) + N * DepthCm;
}

FVector FTDFluidPlaneBasis::UnprojectVector(const FVector2D& PlaneVector) const
{
	if (IsWorldFacing())
	{
		return FVector(PlaneVector.X, PlaneVector.Y, 0.0);
	}
	return U * PlaneVector.X + V * PlaneVector.Y;
}

bool FTDFluidPlaneBasis::IntersectRay(const FVector& RayOrigin, const FVector& RayDirection, double PlaneDepthCm, FVector& OutPoint) const
{
	const double DirectionAlongNormal = FVector::DotProduct(N, RayDirection);
	if (FMath::Abs(DirectionAlongNormal) < MinRayPlaneAlignment)
	{
		return false;
	}

	const double RayDistance = (PlaneDepthCm - FVector::DotProduct(N, RayOrigin)) / DirectionAlongNormal;
	if (RayDistance < 0.0)
	{
		return false;
	}

	OutPoint = RayOrigin + RayDirection * RayDistance;
	return true;
}

FQuat FTDFluidPlaneBasis::GetRotation() const
{
	if (IsWorldFacing())
	{
		return FQuat::Identity;
	}
	return FRotationMatrix::MakeFromXY(U, V).ToQuat();
}

FVector2D FTDFluidPlaneBasis::GetPlaneUpDirection() const
{
	if (IsWorldFacing())
	{
		return FVector2D::ZeroVector;
	}
	return FVector2D(0.0, 1.0);
}

bool FTDFluidPlaneBasis::IsNearlyEqual(const FTDFluidPlaneBasis& Other, double ToleranceDegrees) const
{
	if (IsWorldFacing() != Other.IsWorldFacing())
	{
		return false;
	}
	if (IsWorldFacing())
	{
		return true;
	}

	const double YawDeltaDegrees = FMath::Abs(FMath::FindDeltaAngleDegrees(ResolveYawDegrees(*this), ResolveYawDegrees(Other)));
	const double LeanDeltaDegrees = FMath::Abs(ResolveLeanDegrees(*this) - ResolveLeanDegrees(Other));
	return YawDeltaDegrees <= ToleranceDegrees && LeanDeltaDegrees <= ToleranceDegrees;
}

void FTDFluidPlaneBasis::PreservePivotPlanePoint(const FTDFluidPlaneBasis& PreviousBasis, const FVector& PivotWorld)
{
	PlaneCoordinateOffsetCm = FVector2D::ZeroVector;
	if (IsWorldFacing() || PreviousBasis.IsWorldFacing())
	{
		return;
	}
	PlaneCoordinateOffsetCm = PreviousBasis.ProjectPoint(PivotWorld) - ProjectVector(PivotWorld);
}
