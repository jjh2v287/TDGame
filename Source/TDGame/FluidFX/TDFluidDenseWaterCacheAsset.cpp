#include "FluidFX/TDFluidDenseWaterCacheAsset.h"
#include "Misc/Crc.h"

namespace TDFluidDenseWaterCachePrivate
{
	constexpr double WindowExtentToleranceCm = 1.0;

	uint32 ChainArrayCrc(const TArray<float>& Values, uint32 Crc)
	{
		return FCrc::MemCrc32(Values.GetData(), Values.Num() * sizeof(float), Crc);
	}
}

bool UTDFluidDenseWaterCacheAsset::IsUsable() const
{
	const int32 CellCount = Resolution * Resolution;
	const bool bHasWindow = Resolution > 0 && ExtentCm.X > 0.0 && ExtentCm.Y > 0.0;
	return bHasWindow && DepthCm.Num() == CellCount && FaceVelocityXCmPerSec.Num() == CellCount && FaceVelocityYCmPerSec.Num() == CellCount;
}

bool UTDFluidDenseWaterCacheAsset::MatchesWindow(const FVector2D& WindowCenterCm, const FVector2D& WindowExtentCm) const
{
	using namespace TDFluidDenseWaterCachePrivate;

	if (!IsUsable())
	{
		return false;
	}

	const FVector2D HalfCellCm = ExtentCm / (2.0 * static_cast<double>(Resolution));
	const FVector2D CenterOffsetCm = (WindowCenterCm - CenterCm).GetAbs();
	const FVector2D ExtentOffsetCm = (WindowExtentCm - ExtentCm).GetAbs();
	const bool bCenterMatches = CenterOffsetCm.X <= HalfCellCm.X && CenterOffsetCm.Y <= HalfCellCm.Y;
	const bool bExtentMatches = ExtentOffsetCm.X <= WindowExtentToleranceCm && ExtentOffsetCm.Y <= WindowExtentToleranceCm;
	return bCenterMatches && bExtentMatches;
}

void UTDFluidDenseWaterCacheAsset::SetFromSeed(const FTDFluidDenseWaterSeed& Seed)
{
	using namespace TDFluidDenseWaterCachePrivate;

	Resolution = Seed.Resolution;
	CenterCm = Seed.CenterCm;
	ExtentCm = Seed.ExtentCm;
	DepthCm = Seed.DepthCm;
	FaceVelocityXCmPerSec = Seed.FaceVelocityXCmPerSec;
	FaceVelocityYCmPerSec = Seed.FaceVelocityYCmPerSec;
	const uint32 DepthCrc = ChainArrayCrc(DepthCm, 0);
	const uint32 FaceXCrc = ChainArrayCrc(FaceVelocityXCmPerSec, DepthCrc);
	SourceChecksum = static_cast<int64>(ChainArrayCrc(FaceVelocityYCmPerSec, FaceXCrc));
}

FTDFluidDenseWaterSeed UTDFluidDenseWaterCacheAsset::MakeSeed() const
{
	FTDFluidDenseWaterSeed Seed;
	Seed.Resolution = Resolution;
	Seed.CenterCm = CenterCm;
	Seed.ExtentCm = ExtentCm;
	Seed.DepthCm = DepthCm;
	Seed.FaceVelocityXCmPerSec = FaceVelocityXCmPerSec;
	Seed.FaceVelocityYCmPerSec = FaceVelocityYCmPerSec;
	return Seed;
}
