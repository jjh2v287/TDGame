#include "FluidFX/TDFluidGrid2DSolver.h"

bool FTDFluidGrid2DSolver::UsesDenseDensity() const
{
	if (Config.SimMode != ETDFluidSimMode::FluidSim || IsWaterLayerActive())
	{
		return false;
	}
	return Config.DenseDensityCeiling > 1.0f || Config.DensityAccumulationPerSec > 0.0f;
}

bool FTDFluidGrid2DSolver::UsesFlowNoiseSpeedMask() const
{
	return Config.FlowNoiseAccelerationCmPerSec2 > 0.0f
		&& Config.FlowNoiseSpeedMaskReferenceCmPerSec > 0.0f
		&& !IsWaterLayerActive();
}

float FTDFluidGrid2DSolver::GetStampDensityCeiling() const
{
	if (!UsesDenseDensity())
	{
		return 1.0f;
	}
	return FMath::Max(Config.DenseDensityCeiling, 1.0f);
}

float FTDFluidGrid2DSolver::ComputeFlowNoiseSpeedMask(float SpeedCmPerSec, float InverseReferenceCmPerSec)
{
	if (InverseReferenceCmPerSec <= 0.0f)
	{
		return 1.0f;
	}
	return FMath::Clamp(SpeedCmPerSec * InverseReferenceCmPerSec, 0.0f, 1.0f);
}
