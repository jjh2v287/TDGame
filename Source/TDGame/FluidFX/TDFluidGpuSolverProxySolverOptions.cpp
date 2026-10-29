#include "FluidFX/TDFluidGpuSolverProxy.h"

void FTDFluidGpuSolverProxy::SetAdvectionOffsetPlane(const FVector2D& InAdvectionOffsetPlaneCmPerSec)
{
	AdvectionOffsetPlaneCmPerSec = InAdvectionOffsetPlaneCmPerSec.ContainsNaN() ? FVector2D::ZeroVector : InAdvectionOffsetPlaneCmPerSec;
}

void FTDFluidGpuSolverProxy::SetNoiseLatticeOffsetCells(const FIntPoint& InNoiseLatticeOffsetCells)
{
	NoiseLatticeOffsetCells = InNoiseLatticeOffsetCells;
}

bool FTDFluidGpuSolverProxy::MoveWindowWithoutShift(const FVector2D& NewCenterPlaneCm)
{
	if (!IsInitialized() || NewCenterPlaneCm.ContainsNaN() || Settings.bUsesWaterLayer || Settings.AreaAddressModeRequest == TDFluidGpuSolverConstants::WrapAreaAddressModeRequest)
	{
		return false;
	}

	if (NewCenterPlaneCm == CenterPlaneCm)
	{
		return false;
	}

	CenterPlaneCm = NewCenterPlaneCm;
	return true;
}
