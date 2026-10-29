#include "FluidFX/TDFluidLiveComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarTDFluidFoliageBend(
	TEXT("TD.FluidFX.FoliageBend"),
	-1,
	TEXT("Foliage bend vector layer. 0 turns it off and resets it, any other value follows the preset."),
	ECVF_Default);

namespace TDFluidLiveComponentFoliageBendPrivate
{
	bool bHasWarnedAboutNonWorldPlane = false;

	FVector2D ComputeFoliageBendCellSizeCm(const FVector2D& ExtentCm, int32 Resolution)
	{
		return Resolution > 0 ? ExtentCm / static_cast<double>(Resolution) : FVector2D::ZeroVector;
	}
}

bool UTDFluidLiveComponent::IsFoliageBendAuxActive() const
{
	return SimulationConfig.bEnableBendVectorChannel && !SimulationConfig.bAnchorsDensityToActor && PlaneBasis.IsWorldFacing();
}

void UTDFluidLiveComponent::RefreshFoliageBendStep(float DeltaTime)
{
	using namespace TDFluidLiveComponentFoliageBendPrivate;

	PreparedFoliageBendDeltaSeconds = DeltaTime;
	if (SimulationConfig.bEnableBendVectorChannel && !PlaneBasis.IsWorldFacing() && !bHasWarnedAboutNonWorldPlane)
	{
		bHasWarnedAboutNonWorldPlane = true;
		UE_LOG(LogTDFluidFoliageBend, Warning, TEXT("%s enables the foliage bend vector on a plane that does not face the world, so the bend layer stays off."), *GetPathName());
	}

	bIsFoliageBendStepEnabled = FTDFluidFoliageBendLayer::ResolveEnabled(CVarTDFluidFoliageBend.GetValueOnAnyThread(), IsFoliageBendAuxActive());
	if (!bIsFoliageBendStepEnabled)
	{
		FoliageBendLayer.RequestReset();
	}
}

void UTDFluidLiveComponent::AdvanceFoliageBendCpu(float DeltaTime)
{
	using namespace TDFluidLiveComponentFoliageBendPrivate;

	if (!bIsFoliageBendStepEnabled || bUsesGpuSolver || !Solver.IsInitialized())
	{
		return;
	}

	const int32 Resolution = Solver.GetResolution();
	const FTDFluidSimulationConfig& SolverConfig = Solver.GetConfig();
	const FVector2D CellSizeCm = ComputeFoliageBendCellSizeCm(Solver.GetExtentCm(), Resolution);
	const bool bHasWaterSurface = SolverConfig.bEnableWaterLayer && PackedWaterSurfacePixels.Num() == Resolution * Resolution;
	const FTDFluidFoliageBendParameters Parameters = FTDFluidFoliageBendLayer::MakeParameters(SolverConfig, DeltaTime, CellSizeCm, bHasWaterSurface);
	FoliageBendLayer.AdvanceCpu(PackedVelocityPixels, PackedDensityPressurePixels, PackedWaterSurfacePixels, Resolution, Solver.GetCenterWorld(), CellSizeCm, Parameters);
}

void UTDFluidLiveComponent::SubmitFoliageBendGpu()
{
	using namespace TDFluidLiveComponentFoliageBendPrivate;

	const float DeltaSeconds = PreparedFoliageBendDeltaSeconds;
	PreparedFoliageBendDeltaSeconds = 0.0f;
	if (!bUsesGpuSolver || !bIsFoliageBendStepEnabled || DeltaSeconds <= 0.0f)
	{
		return;
	}

	const int32 Resolution = GpuSolverProxy.GetResolution();
	const FVector2D CellSizeCm = ComputeFoliageBendCellSizeCm(GpuSolverProxy.GetExtentCm(), Resolution);
	UTexture* BendWaterSurfaceTexture = IsUsingGpuWaterLayer() ? GetWaterSurfaceOutputTexture() : nullptr;
	const FTDFluidFoliageBendParameters Parameters = FTDFluidFoliageBendLayer::MakeParameters(SimulationConfig, DeltaSeconds, CellSizeCm, BendWaterSurfaceTexture != nullptr);
	const bool bPreservesAuxRedGreen = IsHeatAuxOutputActive() || IsObstacleDistanceAuxActive();
	FoliageBendLayer.SubmitGpu(
		GetVelocityTexture(),
		GetDensityPressureTexture(),
		BendWaterSurfaceTexture,
		Cast<UTextureRenderTarget2D>(GetAuxTexture()),
		bPreservesAuxRedGreen,
		Resolution,
		GetSimulationCenterWorld(),
		CellSizeCm,
		Parameters);
}
