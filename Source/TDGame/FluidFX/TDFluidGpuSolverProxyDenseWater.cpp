#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "TDFluidGpuDenseWater.h"

namespace
{
	bool TDFluidProxyDenseWaterDoesSeedMatchWindow(const FTDFluidDenseWaterSeed& Seed, const FVector2D& CenterCm, const FVector2D& ExtentCm)
	{
		const int32 SeedCellCount = Seed.Resolution * Seed.Resolution;
		const bool bHasSeedData = Seed.Resolution > 0
			&& Seed.DepthCm.Num() == SeedCellCount
			&& Seed.FaceVelocityXCmPerSec.Num() == SeedCellCount
			&& Seed.FaceVelocityYCmPerSec.Num() == SeedCellCount;
		if (!bHasSeedData || Seed.ExtentCm.X <= 0.0 || Seed.ExtentCm.Y <= 0.0)
		{
			return false;
		}

		constexpr double ExtentToleranceCm = 1.0;
		const FVector2D HalfSeedCellCm = Seed.ExtentCm / (2.0 * static_cast<double>(Seed.Resolution));
		const FVector2D CenterOffsetCm = (CenterCm - Seed.CenterCm).GetAbs();
		const FVector2D ExtentOffsetCm = (ExtentCm - Seed.ExtentCm).GetAbs();
		return CenterOffsetCm.X <= HalfSeedCellCm.X && CenterOffsetCm.Y <= HalfSeedCellCm.Y
			&& ExtentOffsetCm.X <= ExtentToleranceCm && ExtentOffsetCm.Y <= ExtentToleranceCm;
	}
}

void FTDFluidGpuSolverProxy::ApplyDenseWaterSettings(const FTDFluidSimulationConfig& Config, FTDFluidGpuSimulationSettings& InOutSettings)
{
	InOutSettings.bUsesDenseWater = Config.bEnableDenseWater && InOutSettings.bUsesWaterLayer && FTDFluidGrid2DSolver::IsDenseWaterConsoleEnabled();
	FTDFluidGpuDenseWaterSettings& DenseWater = InOutSettings.DenseWater;
	DenseWater.GravityCmPerSec2 = Config.DenseWaterGravityCmPerSec2;
	DenseWater.GapFillTendency = Config.DenseWaterGapFillTendency;
	DenseWater.PeakAvoidance = Config.DenseWaterPeakAvoidance;
	DenseWater.PeakSoftnessCm = Config.DenseWaterPeakSoftnessCm;
	DenseWater.SdfRepelCmPerSec2 = Config.DenseWaterSdfRepelCmPerSec2;
	DenseWater.SdfRepelBandCm = Config.DenseWaterSdfRepelBandCm;
	DenseWater.LinearDragPerSec = Config.DenseWaterLinearDragPerSec;
	DenseWater.BedFriction = Config.DenseWaterBedFriction;
	DenseWater.WetThresholdCm = Config.DenseWaterWetThresholdCm;
	DenseWater.DryDepthCm = Config.DenseWaterDryDepthCm;
	DenseWater.FilmDrainCmPerSec = Config.DenseWaterFilmDrainCmPerSec;
	DenseWater.MaxFlowDepthCm = Config.DenseWaterMaxFlowDepthCm;
	DenseWater.MaxSpeedCmPerSec = Config.DenseWaterMaxSpeedCmPerSec;
	DenseWater.CourantNumber = Config.DenseWaterCourantNumber;
	DenseWater.MaxSubsteps = Config.DenseWaterMaxSubsteps;
	DenseWater.MomentumAdvection = Config.DenseWaterMomentumAdvection;
	DenseWater.ReservoirRelaxPerSec = Config.DenseWaterReservoirRelaxPerSec;
	FTDFluidGpuDenseWaterRules::Sanitize(DenseWater);
	if (InOutSettings.bUsesDenseWater)
	{
		InOutSettings.bTreatDryCellsAsSolid = false;
	}
}

void FTDFluidGpuSolverProxy::SetDenseWaterSeed(const FTDFluidDenseWaterSeed& Seed)
{
	DenseWaterSeed = Seed;
	bHasPendingDenseWaterSeed = true;
}

void FTDFluidGpuSolverProxy::ClearDenseWaterSeed()
{
	DenseWaterSeed.Reset();
	bHasPendingDenseWaterSeed = false;
}

void FTDFluidGpuSolverProxy::GatherDenseWaterSeed(FTDFluidGpuActorInput& OutActorInput)
{
	if (!bHasPendingDenseWaterSeed || !DenseWaterSeed.IsSet() || !OutActorInput.Settings.bUsesDenseWater)
	{
		return;
	}

	bHasPendingDenseWaterSeed = false;
	const FTDFluidDenseWaterSeed& Seed = DenseWaterSeed.GetValue();
	if (!TDFluidProxyDenseWaterDoesSeedMatchWindow(Seed, CenterPlaneCm, Settings.ExtentCm))
	{
		return;
	}

	const int32 SeedCellCount = Seed.Resolution * Seed.Resolution;
	OutActorInput.DenseSeedResolution = Seed.Resolution;
	OutActorInput.DenseSeedTexels.SetNumUninitialized(SeedCellCount);
	for (int32 SeedIndex = 0; SeedIndex < SeedCellCount; ++SeedIndex)
	{
		OutActorInput.DenseSeedTexels[SeedIndex] = FVector4f(Seed.DepthCm[SeedIndex], Seed.FaceVelocityXCmPerSec[SeedIndex], Seed.FaceVelocityYCmPerSec[SeedIndex], 0.0f);
	}
}
