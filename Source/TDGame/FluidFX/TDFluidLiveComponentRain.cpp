#include "FluidFX/TDFluidLiveComponent.h"

#include "Engine/World.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidRainImpacts.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DECLARE_CATEGORY_EXTERN(TDFluid);

namespace TDFluidLiveComponentRainPrivate
{
	TAutoConsoleVariable<int32> CVarTDFluidRain(
		TEXT("TD.FluidFX.Rain"),
		-1,
		TEXT("Rain impacts on fluid components: -1 follows the preset rain settings, 0 disables rain everywhere."),
		ECVF_Default);
}

bool UTDFluidLiveComponent::IsRainActive() const
{
	if (!RainSettings.bEnabled || TDFluidLiveComponentRainPrivate::CVarTDFluidRain.GetValueOnGameThread() == 0)
	{
		return false;
	}
	return PlaneBasis.IsWorldFacing() && IsSimulationInitialized();
}

float UTDFluidLiveComponent::ResolveRainIntensity() const
{
	if (!IsRainActive())
	{
		return 0.0f;
	}

	const UWorld* World = GetWorld();
	const UTDFluidLiveSubsystem* Subsystem = World ? World->GetSubsystem<UTDFluidLiveSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetEffectiveRainIntensity() : 0.0f;
}

void UTDFluidLiveComponent::QueueRainForStep(float DeltaTime)
{
	if (!IsRainActive())
	{
		Solver.SetRainWetnessPerSec(0.0f);
		GpuSolverProxy.SetRainWetnessPerSec(0.0f);
		NearRippleLayer.SetRainInput(FTDFluidNearRippleRainInput());
		RainImpactState.PuddleLevelCm = 0.0f;
		RainImpactState.LastQueuedDropCount = 0;
		RainImpactState.LastCappedDropCount = 0;
		return;
	}

	const float Intensity = ResolveRainIntensity();
	const bool bUsesStampPath = !bUsesGpuSolver || IsUsingGpuWaterLayer() || !AcceptsMassInput();
	int32 MaxDropCount = RainSettings.MaxDropsPerTick;
	if (bUsesStampPath)
	{
		MaxDropCount = FMath::Min(MaxDropCount, FMath::Max(SimulationConfig.MaxPointsPerTick, 1) - PendingBrushStamps.Num());
	}
	MaxDropCount = FMath::Max(MaxDropCount, 0);

	TArray<FTDFluidMassInputPoint> RainPoints;
	{
		CSV_SCOPED_TIMING_STAT(TDFluid, RainGenerate);
		FTDFluidRainImpacts::GenerateImpacts(RainSettings, Intensity, DeltaTime, GetSimulationWindowCenterWorld(), GetSimulationExtentCm(), MaxDropCount, RainImpactState, RainPoints);
	}
	QueueMassInputPoints(RainPoints, ETDFluidBrushSource::Rain);

	const float WetnessPerSec = Intensity * RainSettings.WetnessFromRainPerSec;
	Solver.SetRainWetnessPerSec(WetnessPerSec);
	GpuSolverProxy.SetRainWetnessPerSec(WetnessPerSec);

	FTDFluidNearRippleRainInput NearRainInput;
	NearRainInput.DropsPerSquareMeterPerSec = Intensity * RainSettings.DropsPerSquareMeterPerSec;
	NearRainInput.DropImpulseCm = RainSettings.NearDropImpulseCm;
	NearRainInput.DropRadiusCm = RainSettings.DropRadiusCm;
	NearRainInput.RandomSeed = static_cast<uint32>(RainSettings.RandomSeed);
	NearRippleLayer.SetRainInput(NearRainInput);

	RainImpactState.PuddleLevelCm = FTDFluidRainImpacts::AdvancePuddleLevel(RainSettings, Intensity, DeltaTime, RainImpactState.PuddleLevelCm);
	CSV_CUSTOM_STAT(TDFluid, RainDropsQueued, RainImpactState.LastQueuedDropCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(TDFluid, RainDropsCapped, RainImpactState.LastCappedDropCount, ECsvCustomStatOp::Set);
}
