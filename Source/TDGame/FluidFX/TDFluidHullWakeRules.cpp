#include "FluidFX/TDFluidHullWakeRules.h"

FVector2D FTDFluidHullWakeRules::ResolveHullLengthAndBeam(const FVector& LocalBoundsSizeCm, float LengthCm, float BeamCm)
{
	const double BoundsLengthCm = FMath::Max(LocalBoundsSizeCm.X, LocalBoundsSizeCm.Y);
	const double BoundsBeamCm = FMath::Min(LocalBoundsSizeCm.X, LocalBoundsSizeCm.Y);
	return FVector2D(LengthCm > 0.0f ? LengthCm : BoundsLengthCm, BeamCm > 0.0f ? BeamCm : BoundsBeamCm);
}

void FTDFluidHullWakeRules::BuildHullStamps(const FTransform& HullTransform, const FVector& VelocityCmPerSec, float LengthCm, float BeamCm, ETDFluidBrushChannel Channel, const FTDFluidInputBrushSettings& Settings, TArray<FTDFluidBrushStamp>& OutStamps)
{
	OutStamps.Reset();
	if (LengthCm <= 0.0f || BeamCm <= 0.0f)
	{
		return;
	}

	if (VelocityCmPerSec.Size() < Settings.HullWakeMinSpeedCmPerSec)
	{
		return;
	}

	const int32 StampCount = FMath::Clamp(FMath::CeilToInt(LengthCm / (4.0f * BeamCm)), MinHullStampCount, MaxHullStampCount);
	const float StampRadiusCm = LengthCm / (2.0f * static_cast<float>(StampCount));
	const float StampAspectRatio = FMath::Clamp(StampRadiusCm / (0.5f * BeamCm), 1.0f, 4.0f);
	const FVector HullForward = HullTransform.GetUnitAxis(EAxis::X);
	const FVector HullCenter = HullTransform.GetLocation();
	const int32 BowStampIndex = StampCount - 1;
	OutStamps.Reserve(StampCount);
	for (int32 StampIndex = 0; StampIndex < StampCount; ++StampIndex)
	{
		const float AlongHullCm = LengthCm * ((static_cast<float>(StampIndex) + 0.5f) / static_cast<float>(StampCount) - 0.5f);
		FTDFluidBrushStamp& Stamp = OutStamps.AddDefaulted_GetRef();
		Stamp.WorldLocation = HullCenter + HullForward * AlongHullCm;
		Stamp.WorldVelocity = VelocityCmPerSec;
		Stamp.RadiusCm = StampRadiusCm;
		Stamp.DensityStrength = Settings.HullWakeDensityStrength;
		Stamp.VelocityStrength = Settings.HullWakeVelocityStrength;
		Stamp.Channel = Channel;
		Stamp.ShapeAspectRatio = StampAspectRatio;
		Stamp.ShapeMajorAxis = HullForward;
		Stamp.RippleImpulseCm = StampIndex == BowStampIndex ? Settings.HullWakeRippleImpulseCm : 0.0f;
	}
}
