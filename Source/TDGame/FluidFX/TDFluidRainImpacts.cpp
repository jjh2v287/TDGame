#include "FluidFX/TDFluidRainImpacts.h"

namespace TDFluidRainImpactsPrivate
{
	constexpr uint32 HorizontalAxisSalt = 0x9E3779B9u;
	constexpr uint32 VerticalAxisSalt = 0x85EBCA6Bu;
	constexpr double SquareMetersPerSquareCentimeter = 1.0e-4;
	constexpr float UnitFloatScale = 1.0f / 16777216.0f;

	bool IsUsableStep(float DeltaSeconds)
	{
		return FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.0f;
	}
}

uint32 FTDFluidRainImpacts::HashUint32(uint32 Value)
{
	const uint32 State = Value * 747796405u + 2891336453u;
	const uint32 Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
	return (Word >> 22u) ^ Word;
}

uint32 FTDFluidRainImpacts::HashCombine3(uint32 First, uint32 Second, uint32 Third)
{
	return HashUint32(First ^ HashUint32(Second ^ HashUint32(Third)));
}

float FTDFluidRainImpacts::HashToUnitFloat(uint32 Hash)
{
	return static_cast<float>(Hash >> 8u) * TDFluidRainImpactsPrivate::UnitFloatScale;
}

float FTDFluidRainImpacts::SanitizeIntensity(float Intensity)
{
	if (!(Intensity > 0.0f))
	{
		return 0.0f;
	}

	return FMath::Min(Intensity, 1.0f);
}

int32 FTDFluidRainImpacts::GenerateImpacts(const FTDFluidRainSettings& Settings, float Intensity, float DeltaSeconds, const FVector& WindowCenterWorld, const FVector2D& WindowExtentCm, int32 MaxCount, FTDFluidRainImpactState& InOutState, TArray<FTDFluidMassInputPoint>& OutPoints)
{
	OutPoints.Reset();
	InOutState.LastQueuedDropCount = 0;
	InOutState.LastCappedDropCount = 0;
	if (!TDFluidRainImpactsPrivate::IsUsableStep(DeltaSeconds))
	{
		return 0;
	}

	InOutState.ClockSeconds += DeltaSeconds;
	const double ExtentXCm = FMath::Max(WindowExtentCm.X, 0.0);
	const double ExtentYCm = FMath::Max(WindowExtentCm.Y, 0.0);
	const double DropsPerSquareMeterPerSec = FMath::Max(Settings.DropsPerSquareMeterPerSec, 0.0f);
	const double DropRatePerSec = DropsPerSquareMeterPerSec * SanitizeIntensity(Intensity) * ExtentXCm * ExtentYCm * TDFluidRainImpactsPrivate::SquareMetersPerSquareCentimeter;
	if (!FMath::IsFinite(DropRatePerSec))
	{
		return 0;
	}

	InOutState.ExpectedDropCarry += DropRatePerSec * DeltaSeconds;
	const double WholeDropCount = FMath::FloorToDouble(InOutState.ExpectedDropCarry);
	InOutState.ExpectedDropCarry -= WholeDropCount;

	const int32 DropBudget = FMath::Clamp(MaxCount, 0, FMath::Max(Settings.MaxDropsPerTick, 0));
	const int32 DropCount = static_cast<int32>(FMath::Min(WholeDropCount, static_cast<double>(DropBudget)));
	InOutState.LastQueuedDropCount = DropCount;
	InOutState.LastCappedDropCount = static_cast<int32>(FMath::Min(WholeDropCount - DropCount, static_cast<double>(MAX_int32)));

	const uint32 Seed = static_cast<uint32>(Settings.RandomSeed);
	OutPoints.Reserve(DropCount);
	for (int32 DropIndex = 0; DropIndex < DropCount; ++DropIndex)
	{
		const uint32 DropId = InOutState.DropCounter++;
		const double HorizontalUnit = HashToUnitFloat(HashCombine3(Seed, DropId, TDFluidRainImpactsPrivate::HorizontalAxisSalt));
		const double VerticalUnit = HashToUnitFloat(HashCombine3(Seed, DropId, TDFluidRainImpactsPrivate::VerticalAxisSalt));

		FTDFluidMassInputPoint& Point = OutPoints.AddDefaulted_GetRef();
		Point.WorldLocation = FVector(
			WindowCenterWorld.X + (HorizontalUnit - 0.5) * ExtentXCm,
			WindowCenterWorld.Y + (VerticalUnit - 0.5) * ExtentYCm,
			WindowCenterWorld.Z);
		Point.WorldVelocity = FVector::ZeroVector;
		Point.RadiusCm = Settings.DropRadiusCm;
		Point.DensityStrength = Settings.DropDensityStrength;
		Point.HeatStrength = 0.0f;
		Point.RippleImpulseCm = Settings.DropRippleImpulseCm;
		Point.VelocityStrengthScale = 0.0f;
	}

	return DropCount;
}

float FTDFluidRainImpacts::AdvancePuddleLevel(const FTDFluidRainSettings& Settings, float Intensity, float DeltaSeconds, float LevelCm)
{
	if (!Settings.bShouldAccumulatePuddles)
	{
		return 0.0f;
	}

	const float MaxLevelCm = FMath::Max(Settings.PuddleMaxLevelCm, 0.0f);
	const float CurrentLevelCm = FMath::IsFinite(LevelCm) ? LevelCm : 0.0f;
	if (!TDFluidRainImpactsPrivate::IsUsableStep(DeltaSeconds))
	{
		return FMath::Clamp(CurrentLevelCm, 0.0f, MaxLevelCm);
	}

	const float LevelRateCmPerSec = SanitizeIntensity(Intensity) * Settings.PuddleFillCmPerSec - Settings.PuddleDrainCmPerSec;
	return FMath::Clamp(CurrentLevelCm + LevelRateCmPerSec * DeltaSeconds, 0.0f, MaxLevelCm);
}
