#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidDenseWaterCacheAsset.h"
#include "FluidFX/TDFluidGrid2DSolver.h"

bool UTDFluidLiveComponent::IsDenseWaterActive() const
{
	if (bUsesGpuSolver && GpuSolverProxy.IsInitialized())
	{
		return GpuSolverProxy.UsesDenseWater();
	}

	if (Solver.IsInitialized())
	{
		return Solver.IsDenseWaterActive();
	}

	return SimulationConfig.bEnableDenseWater && SimulationConfig.bEnableWaterLayer && SimulationConfig.SimMode != ETDFluidSimMode::SimplePainter
		&& FTDFluidGrid2DSolver::IsDenseWaterConsoleEnabled();
}

bool UTDFluidLiveComponent::SeedDenseWaterState(const UTDFluidDenseWaterCacheAsset& Cache)
{
	if (!Cache.IsUsable())
	{
		return false;
	}

	const FTDFluidDenseWaterSeed Seed = Cache.MakeSeed();
	Solver.SetDenseWaterSeed(Seed);
	GpuSolverProxy.SetDenseWaterSeed(Seed);
	return true;
}
