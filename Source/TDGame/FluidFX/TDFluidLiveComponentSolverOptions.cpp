#include "FluidFX/TDFluidLiveComponent.h"

#include "FluidFX/TDFluidSolverOptionRules.h"
#include "GameFramework/Actor.h"

bool UTDFluidLiveComponent::AdvanceCpuSolverWindow(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift)
{
	EnsureNoiseLatticeOffsetCells();
	const int32 ReinitializationGeneration = Solver.GetReinitializationGeneration();
	if (ReinitializationGeneration != LastCpuReinitializationGeneration)
	{
		bHasPreviousAnchoredCenter = false;
		LastCpuReinitializationGeneration = ReinitializationGeneration;
	}

	const FTDFluidSimulationConfig& SolverConfig = Solver.GetConfig();
	const FVector2D AdvectionOffset = ComputeSolverAdvectionOffset(DeltaTime, DesiredCenterWorld);
	bool bDidMoveWindow = false;
	if (TDFluidSolverOptionRules::IsDensityAnchoredToActor(SolverConfig))
	{
		bDidMoveWindow = Solver.MoveWindowWithoutShift(DesiredCenterWorld);
	}
	else if (bAllowSlidingShift && !IsAreaWrapped())
	{
		bDidMoveWindow = Solver.ShiftOriginForSlidingWindow(DesiredCenterWorld);
	}

	Solver.SetAdvectionOffsetPlane(AdvectionOffset);
	Solver.SetNoiseLatticeOffsetCells(TDFluidSolverOptionRules::IsNoiseLatticeOffsetEnabled(SolverConfig) ? NoiseLatticeOffsetCells : FIntPoint::ZeroValue);
	PreviousAnchoredCenterPlane = DesiredCenterWorld;
	bHasPreviousAnchoredCenter = true;
	return bDidMoveWindow;
}

bool UTDFluidLiveComponent::AdvanceGpuSolverWindow(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift)
{
	EnsureNoiseLatticeOffsetCells();
	const FVector2D AdvectionOffset = ComputeSolverAdvectionOffset(DeltaTime, DesiredCenterWorld);
	bool bDidMoveWindow = false;
	if (TDFluidSolverOptionRules::IsDensityAnchoredToActor(SimulationConfig))
	{
		bDidMoveWindow = GpuSolverProxy.MoveWindowWithoutShift(DesiredCenterWorld);
	}
	else if (bAllowSlidingShift && !IsAreaWrapped())
	{
		bDidMoveWindow = GpuSolverProxy.ShiftWindowTowards(DesiredCenterWorld) != FIntPoint::ZeroValue;
	}

	GpuSolverProxy.SetAdvectionOffsetPlane(AdvectionOffset);
	GpuSolverProxy.SetNoiseLatticeOffsetCells(TDFluidSolverOptionRules::IsNoiseLatticeOffsetEnabled(SimulationConfig) ? NoiseLatticeOffsetCells : FIntPoint::ZeroValue);
	PreviousAnchoredCenterPlane = DesiredCenterWorld;
	bHasPreviousAnchoredCenter = true;
	return bDidMoveWindow;
}

FVector2D UTDFluidLiveComponent::ComputeSolverAdvectionOffset(float DeltaTime, const FVector2D& DesiredCenterWorld) const
{
	FTDFluidAdvectionOffsetInputs Inputs;
	Inputs.Config = bUsesGpuSolver ? &SimulationConfig : &Solver.GetConfig();
	Inputs.PlaneBasis = PlaneBasis;
	Inputs.CurrentCenterPlane = DesiredCenterWorld;
	Inputs.PreviousCenterPlane = PreviousAnchoredCenterPlane;
	Inputs.bHasPreviousCenter = bHasPreviousAnchoredCenter;
	Inputs.DeltaTimeSeconds = DeltaTime;
	return TDFluidSolverOptionRules::ComputeAdvectionOffsetPlane(Inputs);
}

void UTDFluidLiveComponent::EnsureNoiseLatticeOffsetCells()
{
	if (bHasNoiseLatticeOffsetCells)
	{
		return;
	}

	const AActor* OwnerActor = GetOwner();
	const FVector SpawnLocationCm = OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;
	const FString OwnerName = OwnerActor ? OwnerActor->GetName() : GetName();
	NoiseLatticeOffsetCells = TDFluidSolverOptionRules::ComputeNoiseLatticeOffsetCells(SpawnLocationCm, EffectiveSettingsPresetKind, OwnerName);
	bHasNoiseLatticeOffsetCells = true;
}
