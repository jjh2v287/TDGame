#include "TDFluidGpuBackendSelection.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "RHIShaderPlatform.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuInputs.h"

namespace
{
	TAutoConsoleVariable<int32> CVarFluidSolverBackend(
		TEXT("TD.FluidFX.SolverBackend"),
		0,
		TEXT("FluidFX solver backend. 0 = Follow the preset setting, 1 = Use the GPU solver when possible, 2 = Force the CPU solver."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpu(
		TEXT("TD.FluidFX.WaterGpu"),
		0,
		TEXT("1 lets FluidFX actors with the water layer run on the GPU solver when the other backend rules allow it. 0 keeps every water layer actor on the CPU solver."),
		ECVF_Default);

	ETDFluidGpuBackendOverride ResolveBackendOverride(int32 ConsoleValue)
	{
		if (ConsoleValue == 1)
		{
			return ETDFluidGpuBackendOverride::ForceGpuWhenPossible;
		}
		if (ConsoleValue == 2)
		{
			return ETDFluidGpuBackendOverride::ForceCpu;
		}
		return ETDFluidGpuBackendOverride::FollowPreset;
	}

	bool ShouldUseGpuSolver(const FTDFluidGpuBackendQuery& Query, const FTDFluidGpuBackendEnvironment& Environment)
	{
		if (Environment.Override == ETDFluidGpuBackendOverride::ForceCpu)
		{
			return false;
		}
		if (!Environment.bHasRenderingRhi)
		{
			return false;
		}
		if (!Environment.bSupportsShaderModel5)
		{
			return false;
		}
		if (Query.bIsWaterLayerActive && !Environment.bAllowsWaterLayerOnGpu)
		{
			return false;
		}
		if (Query.bIsSimplePainter && Query.bWritesPainterVelocity && !Environment.bSupportsPainterVelocityTypedUavLoad)
		{
			return false;
		}
		if (Environment.Override == ETDFluidGpuBackendOverride::ForceGpuWhenPossible)
		{
			return true;
		}
		if (Query.Preference == ETDFluidGpuSolverPreference::Gpu)
		{
			return true;
		}
		return Query.Preference == ETDFluidGpuSolverPreference::Auto && Query.RequestedResolution > TDFluidGpuSolverConstants::CpuMaximumResolution;
	}
}

FTDFluidGpuBackendEnvironment FTDFluidGpuBackendSelection::CaptureEnvironment()
{
	FTDFluidGpuBackendEnvironment Environment;
	Environment.Override = ResolveBackendOverride(CVarFluidSolverBackend.GetValueOnAnyThread());
	Environment.bAllowsWaterLayerOnGpu = CVarTDFluidWaterGpu.GetValueOnAnyThread() == 1;
	Environment.bHasRenderingRhi = !GUsingNullRHI && FApp::CanEverRender();
	if (!Environment.bHasRenderingRhi)
	{
		return Environment;
	}

	Environment.bSupportsShaderModel5 = IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5);
	Environment.bSupportsPainterVelocityTypedUavLoad = RHIIsTypedUAVLoadSupported(PF_G32R32F);
	return Environment;
}

FTDFluidGpuBackendDecision FTDFluidGpuBackendSelection::Decide(const FTDFluidGpuBackendQuery& Query, const FTDFluidGpuBackendEnvironment& Environment)
{
	FTDFluidGpuBackendDecision Decision;
	Decision.Resolution = FMath::Clamp(Query.RequestedResolution, TDFluidGpuSolverConstants::MinimumResolution, TDFluidGpuSolverConstants::CpuMaximumResolution);
	if (!ShouldUseGpuSolver(Query, Environment))
	{
		if (Query.CpuFallbackResolution > 0)
		{
			Decision.Resolution = FMath::Clamp(Query.CpuFallbackResolution, TDFluidGpuSolverConstants::MinimumResolution, TDFluidGpuSolverConstants::CpuMaximumResolution);
		}
		return Decision;
	}

	Decision.bUsesGpu = true;
	Decision.Resolution = (Query.bIsWaterLayerActive && !Query.bIsSimplePainter)
		? FTDFluidGpuBatch::ResolveWaterResolutionTier(Query.RequestedResolution)
		: FTDFluidGpuBatch::ResolveResolutionTier(Query.RequestedResolution);
	return Decision;
}
