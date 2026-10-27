#include "FluidFX/TDFluidInputBrushRules.h"

#include "FluidFX/TDFluidPlaneBasis.h"

bool FTDFluidInputBrushRules::HasActiveRules(const FTDFluidInputBrushSettings& Settings)
{
	return Settings.VelocityOnlySources != ETDFluidVelocityOnlySources::Off
		|| Settings.bShouldInvertInputDensity
		|| Settings.DensityFadeBelowSpeedCmPerSec > 0.0f
		|| Settings.BrushHeightFadeDistanceCm > 0.0f;
}

bool FTDFluidInputBrushRules::IsVelocityOnlySource(ETDFluidVelocityOnlySources Selection, ETDFluidBrushSource Source)
{
	switch (Selection)
	{
	case ETDFluidVelocityOnlySources::PointArrays:
		return Source == ETDFluidBrushSource::PointArray;
	case ETDFluidVelocityOnlySources::DataChannel:
		return Source == ETDFluidBrushSource::DataChannel;
	case ETDFluidVelocityOnlySources::Fragments:
		return Source == ETDFluidBrushSource::Fragment;
	case ETDFluidVelocityOnlySources::PointArraysAndFragments:
		return Source == ETDFluidBrushSource::PointArray || Source == ETDFluidBrushSource::Fragment;
	case ETDFluidVelocityOnlySources::All:
		return true;
	default:
		return false;
	}
}

float FTDFluidInputBrushRules::ComputeSlowSpeedDensityFactor(float PlanarSpeedCmPerSec, float FadeBelowSpeedCmPerSec)
{
	if (FadeBelowSpeedCmPerSec <= 0.0f)
	{
		return 1.0f;
	}
	return FMath::Clamp(PlanarSpeedCmPerSec / FadeBelowSpeedCmPerSec, 0.0f, 1.0f);
}

float FTDFluidInputBrushRules::ComputeHeightFadeFactor(double HeightAboveSurfaceCm, float FadeDistanceCm)
{
	if (FadeDistanceCm <= 0.0f)
	{
		return 1.0f;
	}
	return static_cast<float>(1.0 - FMath::Clamp(FMath::Abs(HeightAboveSurfaceCm) / static_cast<double>(FadeDistanceCm), 0.0, 1.0));
}

void FTDFluidInputBrushRules::ApplyToStamps(TArray<FTDFluidBrushStamp>& InOutStamps, const FTDFluidInputBrushSettings& Settings, const FTDFluidPlaneBasis& PlaneBasis, double SurfaceDepthCm)
{
	if (!HasActiveRules(Settings))
	{
		return;
	}

	const bool bFadesByHeight = Settings.BrushHeightFadeDistanceCm > 0.0f;
	int32 KeptCount = 0;
	for (int32 Index = 0; Index < InOutStamps.Num(); ++Index)
	{
		FTDFluidBrushStamp Stamp = InOutStamps[Index];
		ApplyDensityRules(Stamp, Settings, PlaneBasis);
		if (bFadesByHeight)
		{
			const double GroundDepthCm = Stamp.bHasGroundElevation ? Stamp.GroundElevationCm : SurfaceDepthCm;
			const float HeightFactor = ComputeHeightFadeFactor(PlaneBasis.ProjectDepth(Stamp.WorldLocation) - GroundDepthCm, Settings.BrushHeightFadeDistanceCm);
			if (HeightFactor <= 0.0f)
			{
				continue;
			}
			ScaleStampStrengths(Stamp, HeightFactor);
		}
		InOutStamps[KeptCount] = Stamp;
		++KeptCount;
	}

	InOutStamps.SetNum(KeptCount, EAllowShrinking::No);
}

void FTDFluidInputBrushRules::ApplyDensityRules(FTDFluidBrushStamp& InOutStamp, const FTDFluidInputBrushSettings& Settings, const FTDFluidPlaneBasis& PlaneBasis)
{
	if (IsVelocityOnlySource(Settings.VelocityOnlySources, InOutStamp.Source))
	{
		InOutStamp.DensityStrength = 0.0f;
		InOutStamp.HeatStrength = 0.0f;
		InOutStamp.RingInnerEraseRatePerSec = 0.0f;
	}

	if (Settings.bShouldInvertInputDensity && InOutStamp.DensityStrength != 0.0f)
	{
		InOutStamp.DensityStrength = -InOutStamp.DensityStrength;
	}

	if (Settings.DensityFadeBelowSpeedCmPerSec > 0.0f)
	{
		const float PlanarSpeedCmPerSec = static_cast<float>(PlaneBasis.ProjectVector(InOutStamp.WorldVelocity).Size());
		InOutStamp.DensityStrength *= ComputeSlowSpeedDensityFactor(PlanarSpeedCmPerSec, Settings.DensityFadeBelowSpeedCmPerSec);
	}
}

void FTDFluidInputBrushRules::ScaleStampStrengths(FTDFluidBrushStamp& InOutStamp, float Factor)
{
	InOutStamp.DensityStrength *= Factor;
	InOutStamp.VelocityStrength *= Factor;
	InOutStamp.RadialVelocityCmPerSec *= Factor;
	InOutStamp.HeatStrength *= Factor;
	InOutStamp.RippleImpulseCm *= Factor;
}
