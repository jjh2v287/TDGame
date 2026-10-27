#include "FluidFX/TDFluidTrackSourceComponent.h"

bool UTDFluidTrackSourceComponent::ResolvePointWorldLocation(int32 PointIndex, FVector& OutWorldLocation) const
{
	if (!Points.IsValidIndex(PointIndex))
	{
		return false;
	}

	const FTDFluidTrackSourcePoint& Point = Points[PointIndex];
	const USceneComponent* Parent = GetAttachParent();
	if (!Point.SocketName.IsNone() && Parent && Parent->DoesSocketExist(Point.SocketName))
	{
		OutWorldLocation = Parent->GetSocketTransform(Point.SocketName).TransformPosition(Point.LocalOffsetCm);
		return true;
	}

	OutWorldLocation = GetComponentTransform().TransformPosition(Point.LocalOffsetCm);
	return true;
}

float UTDFluidTrackSourceComponent::ResolvePointRadiusCm(int32 PointIndex, float RadiusScale) const
{
	if (!Points.IsValidIndex(PointIndex))
	{
		return 0.0f;
	}
	return Points[PointIndex].RadiusCm * RadiusScale;
}
