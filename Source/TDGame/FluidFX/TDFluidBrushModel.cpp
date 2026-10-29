#include "FluidFX/TDFluidBrushModel.h"

#include "FluidFX/TDFluidBandLimitedNoise.h"
#include "FluidFX/TDFluidPlaneBasis.h"

namespace
{
	constexpr int32 BrushModelJitterStreamX = 1;
	constexpr int32 BrushModelJitterStreamY = 2;
	constexpr int32 BrushModelDensityNoiseStream = 3;
	constexpr int32 BrushModelVelocityNoiseStream = 4;
	constexpr double BrushModelMinimumLeadSpeedCmPerSec = 1.0;
	constexpr double BrushModelMinimumReferenceSpeedCmPerSec = 1.0;
	constexpr double BrushModelMinimumNoiseScaleCm = 1.0;

	uint32 BrushModelHashLattice(int32 Seed, int32 Stream, int32 CellX, int32 CellY)
	{
		using namespace TDFluidDeterministic;
		return CombineHash(CombineHash(CombineHash(static_cast<uint32>(Seed), static_cast<uint32>(Stream)), static_cast<uint32>(CellX)), static_cast<uint32>(CellY));
	}

	float BrushModelSampleLatticeUnit(int32 Seed, int32 Stream, int32 CellX, int32 CellY)
	{
		return (TDFluidDeterministic::HashToSignedUnit(BrushModelHashLattice(Seed, Stream, CellX, CellY)) + 1.0f) * 0.5f;
	}

	float BrushModelSmoothFraction(double Fraction)
	{
		const float Clamped = FMath::Clamp(static_cast<float>(Fraction), 0.0f, 1.0f);
		return Clamped * Clamped * (3.0f - 2.0f * Clamped);
	}

	double BrushModelResolveReferenceSpeed(const FTDFluidInputBrushSettings& Settings)
	{
		return FMath::Max(static_cast<double>(Settings.BrushVelocityReferenceCmPerSec), BrushModelMinimumReferenceSpeedCmPerSec);
	}

	void BrushModelShapeSpeed(FTDFluidBrushStamp& InOutStamp, const FTDFluidInputBrushSettings& Settings, const FTDFluidPlaneBasis& PlaneBasis)
	{
		const bool bReshapesSpeed = Settings.BrushVelocityExponent != 1.0f;
		const bool bClampsSpeed = Settings.BrushVelocityClampCmPerSec > 0.0f;
		if (!bReshapesSpeed && !bClampsSpeed)
		{
			return;
		}

		const double PlaneSpeed = PlaneBasis.ProjectVector(InOutStamp.WorldVelocity).Size();
		if (PlaneSpeed <= 0.0)
		{
			return;
		}

		const double ReferenceSpeed = BrushModelResolveReferenceSpeed(Settings);
		const double ShapedSpeed = bReshapesSpeed ? ReferenceSpeed * FMath::Pow(PlaneSpeed / ReferenceSpeed, static_cast<double>(Settings.BrushVelocityExponent)) : PlaneSpeed;
		const double ClampedSpeed = bClampsSpeed ? FMath::Min(ShapedSpeed, static_cast<double>(Settings.BrushVelocityClampCmPerSec)) : ShapedSpeed;
		InOutStamp.WorldVelocity *= ClampedSpeed / PlaneSpeed;
	}

	FVector2D BrushModelOffsetPlanePosition(const FVector2D& PlanePosition, const FVector2D& PlaneVelocity, double PlaneSpeed, float RadiusCm, const FTDFluidInputBrushSettings& Settings)
	{
		FVector2D OffsetPosition = PlanePosition;
		if (Settings.MotionLeadRatio > 0.0f && PlaneSpeed >= BrushModelMinimumLeadSpeedCmPerSec)
		{
			OffsetPosition += PlaneVelocity * (static_cast<double>(Settings.MotionLeadRatio) * RadiusCm / PlaneSpeed);
		}

		if (Settings.PositionJitterRatio <= 0.0f)
		{
			return OffsetPosition;
		}

		const double SpeedFactor = Settings.bJitterScalesWithSpeed ? FMath::Clamp(PlaneSpeed / BrushModelResolveReferenceSpeed(Settings), 0.0, 1.0) : 1.0;
		const double JitterDistanceCm = static_cast<double>(Settings.PositionJitterRatio) * RadiusCm * SpeedFactor;
		if (JitterDistanceCm <= 0.0)
		{
			return OffsetPosition;
		}
		return OffsetPosition + FTDFluidBrushModel::ComputeJitterOffset(OffsetPosition, Settings.BrushNoiseSeed) * JitterDistanceCm;
	}

	void BrushModelRotatePlaneVelocity(FTDFluidBrushStamp& InOutStamp, const FVector2D& PlaneVelocity, float AngleRadians, const FTDFluidPlaneBasis& PlaneBasis)
	{
		float SinAngle = 0.0f;
		float CosAngle = 1.0f;
		FMath::SinCos(&SinAngle, &CosAngle, AngleRadians);
		const FVector2D RotatedVelocity(PlaneVelocity.X * CosAngle - PlaneVelocity.Y * SinAngle, PlaneVelocity.X * SinAngle + PlaneVelocity.Y * CosAngle);
		InOutStamp.WorldVelocity += PlaneBasis.UnprojectVector(RotatedVelocity - PlaneVelocity);
	}
}

bool FTDFluidBrushModel::HasActiveModel(const FTDFluidInputBrushSettings& Settings)
{
	return Settings.GlobalBrushRadiusScale != 1.0f
		|| Settings.PivotBrushRadiusScale != 1.0f
		|| Settings.BoneBrushRadiusScale != 1.0f
		|| Settings.PointerBrushRadiusScale != 1.0f
		|| Settings.ParticleBrushRadiusScale != 1.0f
		|| Settings.FragmentBrushRadiusScale != 1.0f
		|| Settings.SharedBrushRadiusScale != 1.0f
		|| Settings.BrushVelocityExponent != 1.0f
		|| Settings.BrushVelocityClampCmPerSec > 0.0f
		|| Settings.MotionLeadRatio > 0.0f
		|| Settings.PositionJitterRatio > 0.0f
		|| Settings.DensityNoiseAmount > 0.0f
		|| Settings.VelocityNoiseAmount > 0.0f
		|| Settings.SharedInputDensityScale != 1.0f
		|| Settings.SharedInputVelocityScale != 1.0f
		|| Settings.bAdditiveStrokeBlend;
}

float FTDFluidBrushModel::ResolveSourceRadiusScale(const FTDFluidInputBrushSettings& Settings, ETDFluidBrushSource Source)
{
	switch (Source)
	{
	case ETDFluidBrushSource::Pivot:
		return Settings.PivotBrushRadiusScale;
	case ETDFluidBrushSource::Bone:
		return Settings.BoneBrushRadiusScale;
	case ETDFluidBrushSource::Pointer:
		return Settings.PointerBrushRadiusScale;
	case ETDFluidBrushSource::PointArray:
		return Settings.ParticleBrushRadiusScale;
	case ETDFluidBrushSource::Fragment:
		return Settings.FragmentBrushRadiusScale;
	case ETDFluidBrushSource::Shared:
		return Settings.SharedBrushRadiusScale;
	default:
		return 1.0f;
	}
}

float FTDFluidBrushModel::SampleValueNoise(const FVector2D& PositionCm, float ScaleCm, int32 Seed, int32 Stream)
{
	const double InverseScale = 1.0 / FMath::Max(static_cast<double>(ScaleCm), BrushModelMinimumNoiseScaleCm);
	const double LatticeX = PositionCm.X * InverseScale;
	const double LatticeY = PositionCm.Y * InverseScale;
	const int32 CellX = FMath::FloorToInt32(LatticeX);
	const int32 CellY = FMath::FloorToInt32(LatticeY);
	const float BlendX = BrushModelSmoothFraction(LatticeX - static_cast<double>(CellX));
	const float BlendY = BrushModelSmoothFraction(LatticeY - static_cast<double>(CellY));
	const float LowerRow = FMath::Lerp(BrushModelSampleLatticeUnit(Seed, Stream, CellX, CellY), BrushModelSampleLatticeUnit(Seed, Stream, CellX + 1, CellY), BlendX);
	const float UpperRow = FMath::Lerp(BrushModelSampleLatticeUnit(Seed, Stream, CellX, CellY + 1), BrushModelSampleLatticeUnit(Seed, Stream, CellX + 1, CellY + 1), BlendX);
	return FMath::Clamp(FMath::Lerp(LowerRow, UpperRow, BlendY), 0.0f, 1.0f);
}

FVector2D FTDFluidBrushModel::ComputeJitterOffset(const FVector2D& PlanePositionCm, int32 Seed)
{
	const int32 CellX = FMath::FloorToInt32(PlanePositionCm.X);
	const int32 CellY = FMath::FloorToInt32(PlanePositionCm.Y);
	return FVector2D(
		TDFluidDeterministic::HashToSignedUnit(BrushModelHashLattice(Seed, BrushModelJitterStreamX, CellX, CellY)),
		TDFluidDeterministic::HashToSignedUnit(BrushModelHashLattice(Seed, BrushModelJitterStreamY, CellX, CellY)));
}

void FTDFluidBrushModel::ApplyToStamp(FTDFluidBrushStamp& InOutStamp, const FTDFluidInputBrushSettings& Settings, const FTDFluidPlaneBasis& PlaneBasis)
{
	if (InOutStamp.Source == ETDFluidBrushSource::DataChannel)
	{
		return;
	}

	const float RadiusScale = Settings.GlobalBrushRadiusScale * ResolveSourceRadiusScale(Settings, InOutStamp.Source);
	if (RadiusScale != 1.0f)
	{
		InOutStamp.RadiusCm *= RadiusScale;
	}

	BrushModelShapeSpeed(InOutStamp, Settings, PlaneBasis);

	const FVector2D PlaneVelocity = PlaneBasis.ProjectVector(InOutStamp.WorldVelocity);
	const double PlaneSpeed = PlaneVelocity.Size();
	const FVector2D OriginalPlanePosition = PlaneBasis.ProjectPoint(InOutStamp.WorldLocation);
	const FVector2D PlanePosition = BrushModelOffsetPlanePosition(OriginalPlanePosition, PlaneVelocity, PlaneSpeed, InOutStamp.RadiusCm, Settings);
	if (PlanePosition != OriginalPlanePosition)
	{
		InOutStamp.WorldLocation += PlaneBasis.UnprojectVector(PlanePosition - OriginalPlanePosition);
	}

	if (Settings.DensityNoiseAmount > 0.0f)
	{
		const float DensityNoise = SampleValueNoise(PlanePosition, Settings.DensityNoiseScaleCm, Settings.BrushNoiseSeed, BrushModelDensityNoiseStream);
		InOutStamp.DensityStrength *= 1.0f - Settings.DensityNoiseAmount * DensityNoise;
	}

	if (Settings.VelocityNoiseAmount > 0.0f && PlaneSpeed > 0.0)
	{
		const float VelocityNoise = SampleValueNoise(PlanePosition, Settings.VelocityNoiseScaleCm, Settings.BrushNoiseSeed, BrushModelVelocityNoiseStream);
		const float RotationRadians = static_cast<float>((2.0f * VelocityNoise - 1.0f) * Settings.VelocityNoiseAmount * UE_HALF_PI);
		BrushModelRotatePlaneVelocity(InOutStamp, PlaneVelocity, RotationRadians, PlaneBasis);
	}

	if (InOutStamp.Source == ETDFluidBrushSource::Shared)
	{
		InOutStamp.DensityStrength *= Settings.SharedInputDensityScale;
		InOutStamp.VelocityStrength *= Settings.SharedInputVelocityScale;
	}

	if (Settings.bAdditiveStrokeBlend && InOutStamp.DensityStrength > 0.0f)
	{
		InOutStamp.bAddsDensity = true;
		InOutStamp.DensityStrength *= Settings.AdditiveDensityRatePerSec;
	}
}
