#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidWaterWetnessRules.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "RenderGraphBuilder.h"
#include "TDFluidGpuBatch.h"

CSV_DECLARE_CATEGORY_EXTERN(TDFluid);

namespace
{
	constexpr double ProxyFallbackExtentCm = 4000.0;
	constexpr double MinimumExtentCm = 100.0;
	constexpr int32 MaximumRecentSubmissionCount = 8;
	constexpr int32 ProxyWaterFullUploadGatherCount = 3;
	constexpr int32 ProxyWaterMaximumStampsPerTick = 512;
	const FIntPoint ProxyWaterProbeOutsideCell(-1, -1);

	struct FTDFluidReadbackBilinearWeights
	{
		int32 FieldResolution = 0;
		int32 X0 = 0;
		int32 Y0 = 0;
		int32 X1 = 0;
		int32 Y1 = 0;
		float FractionX = 0.0f;
		float FractionY = 0.0f;
	};

	double SanitizeExtentAxisCm(double ExtentAxisCm)
	{
		if (!FMath::IsFinite(ExtentAxisCm))
		{
			return ProxyFallbackExtentCm;
		}
		return FMath::Max(ExtentAxisCm, MinimumExtentCm);
	}

	int32 ComputeReadbackBlockStart(int32 FieldTexel, int32 SimulationResolution)
	{
		return (FieldTexel * SimulationResolution) / TDFluidGpuSolverConstants::ReadbackTileResolution;
	}

	int32 ComputeReadbackBlockEnd(int32 FieldTexel, int32 SimulationResolution)
	{
		return FMath::Max(ComputeReadbackBlockStart(FieldTexel, SimulationResolution) + 1, ComputeReadbackBlockStart(FieldTexel + 1, SimulationResolution));
	}

	FIntPoint ProxyMapWaterProbeGridUVToCell(const FVector2D& GridUV, int32 GridResolution)
	{
		const float GridX = GridUV.X * static_cast<float>(GridResolution) - 0.5f;
		const float GridY = GridUV.Y * static_cast<float>(GridResolution) - 0.5f;
		return FIntPoint(
			FMath::Clamp(FMath::RoundToInt(GridX), 0, GridResolution - 1),
			FMath::Clamp(FMath::RoundToInt(GridY), 0, GridResolution - 1));
	}

	FIntPoint ProxyMapWaterProbePlanePointToCell(const FTDFluidGpuSolverProxy& Proxy, const FVector2D& PlanePoint)
	{
		FVector2D GridUV;
		if (PlanePoint.ContainsNaN() || !Proxy.PlanePointToGridUV(PlanePoint, GridUV))
		{
			return ProxyWaterProbeOutsideCell;
		}
		return ProxyMapWaterProbeGridUVToCell(GridUV, Proxy.GetResolution());
	}

	FTDFluidReadbackBilinearWeights MakeReadbackBilinearWeights(double GridX, double GridY, int32 FieldResolution)
	{
		const double ClampedX = FMath::Clamp(GridX, 0.0, static_cast<double>(FieldResolution - 1));
		const double ClampedY = FMath::Clamp(GridY, 0.0, static_cast<double>(FieldResolution - 1));

		FTDFluidReadbackBilinearWeights Weights;
		Weights.FieldResolution = FieldResolution;
		Weights.X0 = FMath::FloorToInt32(ClampedX);
		Weights.Y0 = FMath::FloorToInt32(ClampedY);
		Weights.X1 = FMath::Min(Weights.X0 + 1, FieldResolution - 1);
		Weights.Y1 = FMath::Min(Weights.Y0 + 1, FieldResolution - 1);
		Weights.FractionX = static_cast<float>(ClampedX - Weights.X0);
		Weights.FractionY = static_cast<float>(ClampedY - Weights.Y0);
		return Weights;
	}

	template <typename ValueType>
	ValueType InterpolateReadbackValue(const TArray<ValueType>& Values, const FTDFluidReadbackBilinearWeights& Weights)
	{
		const int32 RowStart0 = Weights.Y0 * Weights.FieldResolution;
		const int32 RowStart1 = Weights.Y1 * Weights.FieldResolution;
		const ValueType Lower = FMath::Lerp(Values[RowStart0 + Weights.X0], Values[RowStart0 + Weights.X1], Weights.FractionX);
		const ValueType Upper = FMath::Lerp(Values[RowStart1 + Weights.X0], Values[RowStart1 + Weights.X1], Weights.FractionX);
		return FMath::Lerp(Lower, Upper, Weights.FractionY);
	}

	FTDFluidGpuBatchKey MakeEffectiveBatchKey(FTDFluidGpuSimulationSettings SimulationSettings)
	{
		FTDFluidGpuBatch::ApplyWaterConsoleOverrides(SimulationSettings);
		return FTDFluidGpuBatch::MakeKey(SimulationSettings);
	}
}

bool FTDFluidReadbackField::IsValid() const
{
	const int32 CellCount = GetResolution() * GetResolution();
	return PrimaryDensity.Num() == CellCount
		&& SelectiveDensity.Num() == CellCount
		&& VelocityCmPerSec.Num() == CellCount
		&& ExtentCm.X > 0.0
		&& ExtentCm.Y > 0.0;
}

void FTDFluidReadbackField::Reset()
{
	WindowCenterPlaneCm = FVector2D::ZeroVector;
	ExtentCm = FVector2D::ZeroVector;
	FrameNumber = 0;
	PrimaryDensity.Reset();
	SelectiveDensity.Reset();
	VelocityCmPerSec.Reset();
}

void FTDFluidReadbackField::AllocateCells(const FVector2D& InWindowCenterPlaneCm, const FVector2D& InExtentCm, uint32 InFrameNumber)
{
	const int32 CellCount = GetResolution() * GetResolution();
	WindowCenterPlaneCm = InWindowCenterPlaneCm;
	ExtentCm = InExtentCm;
	FrameNumber = InFrameNumber;
	PrimaryDensity.SetNumZeroed(CellCount);
	SelectiveDensity.SetNumZeroed(CellCount);
	VelocityCmPerSec.SetNumZeroed(CellCount);
}

bool FTDFluidReadbackField::CopyFromAtlasTile(
	TConstArrayView<FFloat16Color> AtlasTexels,
	const FIntPoint& AtlasSize,
	const FIntPoint& TileOrigin,
	uint32 InFrameNumber,
	const FVector2D& InWindowCenterPlaneCm,
	const FVector2D& InExtentCm)
{
	const int32 FieldResolution = GetResolution();
	const bool bIsTileInsideAtlas = TileOrigin.X >= 0
		&& TileOrigin.Y >= 0
		&& TileOrigin.X + FieldResolution <= AtlasSize.X
		&& TileOrigin.Y + FieldResolution <= AtlasSize.Y;
	if (!bIsTileInsideAtlas || AtlasTexels.Num() < AtlasSize.X * AtlasSize.Y || InExtentCm.X <= 0.0 || InExtentCm.Y <= 0.0)
	{
		return false;
	}

	AllocateCells(InWindowCenterPlaneCm, InExtentCm, InFrameNumber);
	for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
	{
		const int32 AtlasRowStart = (TileOrigin.Y + FieldY) * AtlasSize.X + TileOrigin.X;
		for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
		{
			const FFloat16Color& Texel = AtlasTexels[AtlasRowStart + FieldX];
			const int32 FieldIndex = FieldY * FieldResolution + FieldX;
			PrimaryDensity[FieldIndex] = Texel.R.GetFloat();
			SelectiveDensity[FieldIndex] = Texel.G.GetFloat();
			VelocityCmPerSec[FieldIndex] = FVector2f(Texel.B.GetFloat(), Texel.A.GetFloat());
		}
	}
	return true;
}

void FTDFluidReadbackField::DownsampleFromSolver(const FTDFluidGrid2DSolver& Solver)
{
	Reset();
	if (!Solver.IsInitialized())
	{
		return;
	}

	const int32 FieldResolution = GetResolution();
	const int32 SimulationResolution = Solver.GetResolution();
	AllocateCells(Solver.GetCenterWorld(), Solver.GetExtentCm(), 0);
	for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
	{
		const int32 StartY = ComputeReadbackBlockStart(FieldY, SimulationResolution);
		const int32 EndY = ComputeReadbackBlockEnd(FieldY, SimulationResolution);
		for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
		{
			const int32 StartX = ComputeReadbackBlockStart(FieldX, SimulationResolution);
			const int32 EndX = ComputeReadbackBlockEnd(FieldX, SimulationResolution);
			float PrimarySum = 0.0f;
			float SelectiveSum = 0.0f;
			FVector2f VelocitySum = FVector2f::ZeroVector;
			for (int32 CellY = StartY; CellY < EndY; ++CellY)
			{
				for (int32 CellX = StartX; CellX < EndX; ++CellX)
				{
					const FTDFluidCellSample Cell = Solver.SampleAtCell(CellX, CellY);
					PrimarySum += Cell.PrimaryDensity;
					SelectiveSum += Cell.SelectiveDensity;
					VelocitySum += FVector2f(Cell.VelocityCmPerSec);
				}
			}

			const float BlockCellCount = static_cast<float>((EndX - StartX) * (EndY - StartY));
			const int32 FieldIndex = FieldY * FieldResolution + FieldX;
			PrimaryDensity[FieldIndex] = PrimarySum / BlockCellCount;
			SelectiveDensity[FieldIndex] = SelectiveSum / BlockCellCount;
			VelocityCmPerSec[FieldIndex] = VelocitySum / BlockCellCount;
		}
	}
}

bool FTDFluidReadbackField::TryMapPlanePointToFieldCell(const FVector2D& PlanePoint, FIntPoint& OutFieldCell) const
{
	OutFieldCell = FIntPoint::ZeroValue;
	if (!IsValid() || PlanePoint.ContainsNaN())
	{
		return false;
	}

	const FVector2D MinPlane = WindowCenterPlaneCm - 0.5 * ExtentCm;
	const double FieldU = (PlanePoint.X - MinPlane.X) / ExtentCm.X;
	const double FieldV = (PlanePoint.Y - MinPlane.Y) / ExtentCm.Y;
	if (FieldU < 0.0 || FieldU > 1.0 || FieldV < 0.0 || FieldV > 1.0)
	{
		return false;
	}

	const int32 FieldResolution = GetResolution();
	OutFieldCell.X = FMath::Clamp(FMath::FloorToInt32(FieldU * FieldResolution), 0, FieldResolution - 1);
	OutFieldCell.Y = FMath::Clamp(FMath::FloorToInt32(FieldV * FieldResolution), 0, FieldResolution - 1);
	return true;
}

FTDFluidCellSample FTDFluidReadbackField::SampleFieldCell(int32 FieldX, int32 FieldY) const
{
	FTDFluidCellSample Sample;
	const int32 FieldResolution = GetResolution();
	if (!IsValid() || FieldX < 0 || FieldX >= FieldResolution || FieldY < 0 || FieldY >= FieldResolution)
	{
		return Sample;
	}

	const int32 FieldIndex = FieldY * FieldResolution + FieldX;
	Sample.PrimaryDensity = PrimaryDensity[FieldIndex];
	Sample.SelectiveDensity = SelectiveDensity[FieldIndex];
	Sample.Velocity = FVector2D(VelocityCmPerSec[FieldIndex]);
	Sample.VelocityCmPerSec = Sample.Velocity;
	return Sample;
}

bool FTDFluidReadbackField::SampleBilinear(const FVector2D& PlanePoint, FTDFluidCellSample& OutSample) const
{
	OutSample = FTDFluidCellSample();
	if (!IsValid() || PlanePoint.ContainsNaN())
	{
		return false;
	}

	const int32 FieldResolution = GetResolution();
	const FVector2D MinPlane = WindowCenterPlaneCm - 0.5 * ExtentCm;
	const double GridX = (PlanePoint.X - MinPlane.X) / ExtentCm.X * FieldResolution - 0.5;
	const double GridY = (PlanePoint.Y - MinPlane.Y) / ExtentCm.Y * FieldResolution - 0.5;
	const FTDFluidReadbackBilinearWeights Weights = MakeReadbackBilinearWeights(GridX, GridY, FieldResolution);

	OutSample.PrimaryDensity = InterpolateReadbackValue(PrimaryDensity, Weights);
	OutSample.SelectiveDensity = InterpolateReadbackValue(SelectiveDensity, Weights);
	OutSample.Velocity = FVector2D(InterpolateReadbackValue(VelocityCmPerSec, Weights));
	OutSample.VelocityCmPerSec = OutSample.Velocity;
	return true;
}

bool FTDFluidReadbackField::ComputeDensityCentroid(bool bUseSelectiveDensity, FVector2D& OutCentroid, float& OutAreaCm2) const
{
	OutCentroid = FVector2D::ZeroVector;
	OutAreaCm2 = 0.0f;
	if (!IsValid())
	{
		return false;
	}

	const int32 FieldResolution = GetResolution();
	const FVector2D FieldCellSizeCm = ExtentCm / static_cast<double>(FieldResolution);
	const FVector2D MinPlane = WindowCenterPlaneCm - 0.5 * ExtentCm;
	const TArray<float>& Densities = bUseSelectiveDensity ? SelectiveDensity : PrimaryDensity;
	double DensitySum = 0.0;
	FVector2D WeightedPosition = FVector2D::ZeroVector;
	for (int32 FieldY = 0; FieldY < FieldResolution; ++FieldY)
	{
		for (int32 FieldX = 0; FieldX < FieldResolution; ++FieldX)
		{
			const double Density = Densities[FieldY * FieldResolution + FieldX];
			if (Density <= 0.0)
			{
				continue;
			}

			const FVector2D FieldCellCenter = MinPlane + FVector2D((FieldX + 0.5) * FieldCellSizeCm.X, (FieldY + 0.5) * FieldCellSizeCm.Y);
			DensitySum += Density;
			WeightedPosition += FieldCellCenter * Density;
		}
	}

	if (DensitySum <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	OutCentroid = WeightedPosition / DensitySum;
	OutAreaCm2 = static_cast<float>(DensitySum * FieldCellSizeCm.X * FieldCellSizeCm.Y);
	return true;
}

ETDFluidGpuSolverPreference FTDFluidGpuSolverProxy::ToSolverPreference(ETDFluidSolverBackend SolverBackend)
{
	switch (SolverBackend)
	{
	case ETDFluidSolverBackend::Gpu:
		return ETDFluidGpuSolverPreference::Gpu;
	case ETDFluidSolverBackend::Auto:
		return ETDFluidGpuSolverPreference::Auto;
	case ETDFluidSolverBackend::Cpu:
	default:
		return ETDFluidGpuSolverPreference::Cpu;
	}
}

FTDFluidGpuSimulationSettings FTDFluidGpuSolverProxy::MakeSimulationSettings(const FTDFluidSimulationConfig& Config, int32 InResolution)
{
	FTDFluidGpuSimulationSettings SimulationSettings;
	SimulationSettings.SimMode = Config.SimMode == ETDFluidSimMode::SimplePainter ? ETDFluidGpuSimMode::SimplePainter : ETDFluidGpuSimMode::FluidSim;
	SimulationSettings.Resolution = InResolution;
	SimulationSettings.ExtentCm = FVector2D(SanitizeExtentAxisCm(Config.SimExtentCm.X), SanitizeExtentAxisCm(Config.SimExtentCm.Y));
	SimulationSettings.VelocityDamping = Config.VelocityDamping;
	SimulationSettings.DensityDissipation = Config.DensityDissipation;
	SimulationSettings.PressureIterations = Config.PressureIterations;
	SimulationSettings.VorticityStrength = Config.VorticityStrength;
	SimulationSettings.TerrainSlopeGravity = Config.TerrainSlopeGravity;
	SimulationSettings.AdvectionSpeedScale = Config.AdvectionSpeedScale;
	SimulationSettings.BorderFadeRatio = Config.BorderFadeRatio;
	SimulationSettings.MaxStampsPerTick = TDFluidGpuSolverConstants::MaximumStampsPerTick;
	SimulationSettings.AmbientDensity = Config.AmbientDensity;
	SimulationSettings.AmbientRefillPerSec = Config.AmbientRefillPerSec;
	SimulationSettings.FlowNoiseAccelerationCmPerSec2 = Config.FlowNoiseAccelerationCmPerSec2;
	SimulationSettings.FlowNoiseScaleCm = Config.FlowNoiseScaleCm;
	SimulationSettings.FlowNoiseSpeed = Config.FlowNoiseSpeed;
	SimulationSettings.AmbientNoiseAmount = Config.AmbientNoiseAmount;
	SimulationSettings.BuoyancyCmPerSec2 = Config.BuoyancyCmPerSec2;
	SimulationSettings.OutflowBandRatio = Config.OutflowBandRatio;
	SimulationSettings.OutflowRatePerSec = Config.OutflowRatePerSec;
	SimulationSettings.bUsesHeatChannel = Config.bEnableHeatChannel && Config.SimMode == ETDFluidSimMode::FluidSim && !Config.bEnableWaterLayer;
	SimulationSettings.HeatCoolingPerSec = Config.HeatCoolingPerSec;
	SimulationSettings.HeatBuoyancyCmPerSec2 = Config.HeatBuoyancyCmPerSec2;
	SimulationSettings.bReflectsAtBoundaries = Config.bReflectsAtBoundaries;
	SimulationSettings.EdgeBounciness = Config.EdgeBounciness;
	SimulationSettings.BorderFadeExponent = Config.BorderFadeExponent;
	SimulationSettings.VortexTangentialAccelerationCmPerSec2 = Config.VortexTangentialAccelerationCmPerSec2;
	SimulationSettings.VortexRadialDriftCmPerSec = Config.VortexRadialDriftCmPerSec;
	SimulationSettings.VortexRadiusRatio = Config.VortexRadiusRatio;
	SimulationSettings.MaxCellSpeedCmPerSec = Config.MaxCellSpeedCmPerSec;
	SimulationSettings.BrushVelocityResponsePerSec = Config.BrushVelocityResponsePerSec;
	SimulationSettings.BrushDisplacementRatio = Config.BrushDisplacementRatio;
	SimulationSettings.BrushEraseRatePerSec = Config.BrushEraseRatePerSec;
	SimulationSettings.PressureSolverModeRequest = static_cast<uint8>(Config.GpuPressureSolverMode);
	SimulationSettings.MultigridCyclesRequest = Config.GpuMultigridCycles;
	SimulationSettings.WaterPressureModeRequest = static_cast<uint8>(Config.WaterGpuPressureMode);
	SimulationSettings.WaterKernelRadius = Config.PressureKernelRadiusTexels;
	SimulationSettings.WaterKernelFeedback = Config.PressureKernelFeedback;
	SimulationSettings.WaterKernelScale = Config.PressureKernelScale;
	SimulationSettings.WaterMultigridCycles = Config.GpuMultigridCycles;
	SimulationSettings.bUsesWaterLayer = Config.bEnableWaterLayer && Config.SimMode == ETDFluidSimMode::FluidSim;
	SimulationSettings.bOpenWaterBorders = Config.bOpenWaterBorders;
	SimulationSettings.bTreatDryCellsAsSolid = Config.bTreatDryCellsAsSolid;
	SimulationSettings.WaterDepthReferenceCm = Config.WaterDepthReferenceCm;
	SimulationSettings.FlowGuideResponsePerSec = Config.FlowGuideResponsePerSec;
	SimulationSettings.WhitewaterFromVorticity = Config.WhitewaterFromVorticity;
	SimulationSettings.WhitewaterVorticityThresholdPerSec = Config.WhitewaterVorticityThresholdPerSec;
	SimulationSettings.RapidsWhitewaterPerSec = Config.RapidsWhitewaterPerSec;
	SimulationSettings.RippleWaveSpeedCmPerSec = Config.RippleWaveSpeedCmPerSec;
	SimulationSettings.RippleDampingPerSec = Config.RippleDampingPerSec;
	SimulationSettings.RippleHeightReferenceCm = Config.RippleHeightReferenceCm;
	SimulationSettings.RippleFlowAdvection = Config.RippleFlowAdvection;
	SimulationSettings.bUsesWaterWetness = Config.bEnableWaterWetness && SimulationSettings.bUsesWaterLayer;
	SimulationSettings.bUsesWhitewaterDissipation = Config.bUseWhitewaterDissipation && SimulationSettings.bUsesWaterLayer;
	SimulationSettings.WhitewaterDissipation = FMath::Clamp(Config.WhitewaterDissipation, 0.0f, 10.0f);
	SimulationSettings.BankWetHeightCm = FMath::Clamp(Config.BankWetHeightCm, 0.0f, 1000.0f);
	SimulationSettings.WetnessFromWhitewater = FMath::Clamp(Config.WetnessFromWhitewater, 0.0f, 1.0f);
	SimulationSettings.SwashHeightCm = FTDFluidWaterWetnessRules::SanitizeSwashHeightCm(Config.SwashHeightCm);
	SimulationSettings.CoastWavePeriodSeconds = FTDFluidWaterWetnessRules::SanitizeCoastWavePeriodSeconds(Config.CoastWavePeriodSeconds);
	SimulationSettings.WetnessDryingSeconds = FTDFluidWaterWetnessRules::SanitizeDryingSeconds(Config.WetnessDryingSeconds);
	if (SimulationSettings.bUsesWaterLayer)
	{
		SimulationSettings.MaxStampsPerTick = FMath::Clamp(Config.MaxPointsPerTick, 1, ProxyWaterMaximumStampsPerTick);
	}
	return SimulationSettings;
}

FTDFluidGpuStampInput FTDFluidGpuSolverProxy::MakeStampInput(const FTDFluidBrushStamp& PlaneStamp, bool bCarriesRippleImpulse)
{
	FTDFluidGpuStampInput StampInput;
	StampInput.CenterPlaneCm = FVector2D(PlaneStamp.WorldLocation.X, PlaneStamp.WorldLocation.Y);
	StampInput.VelocityPlaneCmPerSec = FVector2D(PlaneStamp.WorldVelocity.X, PlaneStamp.WorldVelocity.Y);
	StampInput.RadiusCm = PlaneStamp.RadiusCm;
	StampInput.DensityStrength = PlaneStamp.DensityStrength;
	StampInput.VelocityStrength = PlaneStamp.VelocityStrength;
	StampInput.RadialVelocityCmPerSec = PlaneStamp.RadialVelocityCmPerSec;
	StampInput.HeatStrength = PlaneStamp.HeatStrength;
	StampInput.bWritesSelectiveDensity = PlaneStamp.Channel == ETDFluidBrushChannel::ChannelG_Blood || PlaneStamp.Channel == ETDFluidBrushChannel::ChannelB_Fire;
	StampInput.RippleImpulseCm = bCarriesRippleImpulse ? PlaneStamp.RippleImpulseCm : 0.0f;
	return StampInput;
}

FTDFluidGpuObstacleInput FTDFluidGpuSolverProxy::MakeObstacleInput(const FTDFluidSdfObstacle& PlaneObstacle)
{
	FTDFluidGpuObstacleInput ObstacleInput;
	ObstacleInput.CenterPlaneCm = FVector2D(PlaneObstacle.WorldCenter.X, PlaneObstacle.WorldCenter.Y);
	ObstacleInput.RadiusCm = PlaneObstacle.RadiusCm;
	ObstacleInput.BoxHalfExtentsCm = PlaneObstacle.BoxHalfExtentsCm;
	ObstacleInput.bIsBox = PlaneObstacle.bIsBox;
	return ObstacleInput;
}

int64 FTDFluidGpuSolverProxy::EstimateActorMemoryBytes(ETDFluidSimMode SimMode, int32 RequestedResolution, bool bUsesHeatChannel, bool bUsesTransmittance, ETDFluidGpuPressureSolverMode PressureSolverMode)
{
	const ETDFluidGpuSimMode GpuSimMode = SimMode == ETDFluidSimMode::SimplePainter ? ETDFluidGpuSimMode::SimplePainter : ETDFluidGpuSimMode::FluidSim;
	return FTDFluidGpuBatch::EstimateActorMemory(GpuSimMode, RequestedResolution, bUsesHeatChannel, bUsesTransmittance, static_cast<uint8>(PressureSolverMode)).GetTotalBytes();
}

void FTDFluidGpuSolverProxy::Initialize(const FTDFluidSimulationConfig& Config, int32 InResolution, const FVector2D& InitialCenterPlaneCm)
{
	const bool bUsesWaterResolutionTier = Config.bEnableWaterLayer && Config.SimMode != ETDFluidSimMode::SimplePainter;
	Resolution = bUsesWaterResolutionTier ? FTDFluidGpuBatch::ResolveWaterResolutionTier(InResolution) : FTDFluidGpuBatch::ResolveResolutionTier(InResolution);
	Settings = MakeSimulationSettings(Config, Resolution);
	WaterFieldConfig = Config;
	CenterPlaneCm = InitialCenterPlaneCm.ContainsNaN() ? FVector2D::ZeroVector : InitialCenterPlaneCm;
	if (!Settings.bUsesWaterLayer)
	{
		WaterFieldState.Release();
	}
	ResetState();
}

void FTDFluidGpuSolverProxy::UpdateSettings(const FTDFluidSimulationConfig& Config)
{
	bWasLastSettingsUpdateReset = false;
	if (!IsInitialized())
	{
		return;
	}

	const bool bPreviouslyUsedWaterLayer = Settings.bUsesWaterLayer;
	const FTDFluidGpuBatchKey PreviousBatchKey = MakeEffectiveBatchKey(Settings);
	Settings = MakeSimulationSettings(Config, Resolution);
	bWasLastSettingsUpdateReset = !(MakeEffectiveBatchKey(Settings) == PreviousBatchKey);
	WaterFieldConfig = Config;
	if (bPreviouslyUsedWaterLayer && Settings.bUsesWaterLayer)
	{
		WaterFieldState.UpdateConfig(Config);
		return;
	}

	if (Settings.bUsesWaterLayer)
	{
		WaterFieldState.Initialize(Config, Resolution, FTDFluidGpuBatch::ComputeWaterFieldDivisor(Resolution), CenterPlaneCm, Settings.ExtentCm);
		WaterFieldState.RequestFullUpload(ProxyWaterFullUploadGatherCount);
		return;
	}

	if (bPreviouslyUsedWaterLayer)
	{
		WaterFieldState.Release();
	}
}

void FTDFluidGpuSolverProxy::ResetState()
{
	++Generation;
	SimulationTimeSeconds = 0.0;
	bNeedsReset = true;
	bWasUpdatingWaterWetness = false;
	bIsTerrainElevationActive = false;
	bHasPreparedStep = false;
	PendingShiftCells = FIntPoint::ZeroValue;
	PreparedStamps.Reset();
	PendingTerrainUpdates.Reset();
	RecentSubmissions.Reset();
	ReadbackField.Reset();
	LatestWaterProbeSamples.Reset();
	LatestWaterProbeFrameNumber = 0;
	LatestWaterProbeGeneration = -1;
	if (!Settings.bUsesWaterLayer)
	{
		return;
	}

	WaterFieldState.Initialize(WaterFieldConfig, Resolution, FTDFluidGpuBatch::ComputeWaterFieldDivisor(Resolution), CenterPlaneCm, Settings.ExtentCm);
	WaterFieldState.RequestFullUpload(ProxyWaterFullUploadGatherCount);
}

int32 FTDFluidGpuSolverProxy::GetWaterFieldResolution() const
{
	return WaterFieldState.GetFieldResolution();
}

FVector2D FTDFluidGpuSolverProxy::WaterFieldCellToPlaneCenter(int32 FieldX, int32 FieldY) const
{
	return WaterFieldState.FieldCellToPlaneCenter(FieldX, FieldY);
}

float FTDFluidGpuSolverProxy::GetWaterSurfaceHeightAtFieldCell(int32 FieldX, int32 FieldY) const
{
	return WaterFieldState.GetSurfaceHeightAtFieldCell(FieldX, FieldY);
}

void FTDFluidGpuSolverProxy::SetWaterInputs(const TArray<FTDFluidFlowGuide>& Guides, const TArray<FTDFluidWaterRegion>& Regions)
{
	WaterFieldState.SetFlowGuidesAndRegions(Guides, Regions);
}

void FTDFluidGpuSolverProxy::SetWaterProbePlanePoints(TConstArrayView<FVector2D> PlanePoints, int32 InProbeGeneration)
{
	WaterProbePlanePoints.Reset(PlanePoints.Num());
	WaterProbePlanePoints.Append(PlanePoints.GetData(), PlanePoints.Num());
	WaterProbeGeneration = InProbeGeneration;
}

bool FTDFluidGpuSolverProxy::TryGetWaterProbeSamples(TArray<FTDFluidGpuWaterProbeSample>& OutSamples, uint32& OutFrameNumber) const
{
	OutSamples.Reset();
	OutFrameNumber = 0;
	if (!Settings.bUsesWaterLayer || LatestWaterProbeGeneration != WaterProbeGeneration || LatestWaterProbeSamples.IsEmpty())
	{
		return false;
	}

	OutSamples = LatestWaterProbeSamples;
	OutFrameNumber = LatestWaterProbeFrameNumber;
	return true;
}

FVector2D FTDFluidGpuSolverProxy::CellToPlaneCenter(int32 CellX, int32 CellY) const
{
	if (!IsInitialized())
	{
		return CenterPlaneCm;
	}

	const FVector2D MinPlane = CenterPlaneCm - 0.5f * Settings.ExtentCm;
	const float CellSizeX = Settings.ExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Settings.ExtentCm.Y / static_cast<float>(Resolution);
	return FVector2D(
		MinPlane.X + (static_cast<float>(CellX) + 0.5f) * CellSizeX,
		MinPlane.Y + (static_cast<float>(CellY) + 0.5f) * CellSizeY);
}

bool FTDFluidGpuSolverProxy::PlanePointToGridUV(const FVector2D& PlanePoint, FVector2D& OutUV) const
{
	if (!IsInitialized())
	{
		OutUV = FVector2D::ZeroVector;
		return false;
	}

	const FVector2D MinPlane = CenterPlaneCm - 0.5f * Settings.ExtentCm;
	OutUV.X = (PlanePoint.X - MinPlane.X) / Settings.ExtentCm.X;
	OutUV.Y = (PlanePoint.Y - MinPlane.Y) / Settings.ExtentCm.Y;
	return OutUV.X >= 0.0f && OutUV.X <= 1.0f && OutUV.Y >= 0.0f && OutUV.Y <= 1.0f;
}

FTDFluidCellSample FTDFluidGpuSolverProxy::SampleSimulationCell(int32 CellX, int32 CellY) const
{
	FIntPoint FieldCell;
	if (!IsValidCell(CellX, CellY) || !ReadbackField.TryMapPlanePointToFieldCell(CellToPlaneCenter(CellX, CellY), FieldCell))
	{
		return FTDFluidCellSample();
	}

	return ReadbackField.SampleFieldCell(FieldCell.X, FieldCell.Y);
}

FIntPoint FTDFluidGpuSolverProxy::ShiftWindowTowards(const FVector2D& DesiredCenterPlaneCm)
{
	if (!IsInitialized() || DesiredCenterPlaneCm.ContainsNaN())
	{
		return FIntPoint::ZeroValue;
	}

	const float CellSizeX = Settings.ExtentCm.X / static_cast<float>(Resolution);
	const float CellSizeY = Settings.ExtentCm.Y / static_cast<float>(Resolution);
	const int32 ShiftQuantumCells = Settings.bUsesWaterLayer ? FTDFluidGpuBatch::ComputeWaterShiftQuantumCells(Resolution) : 1;
	const FIntPoint ShiftCells(
		FMath::RoundToInt32((DesiredCenterPlaneCm.X - CenterPlaneCm.X) / CellSizeX / ShiftQuantumCells) * ShiftQuantumCells,
		FMath::RoundToInt32((DesiredCenterPlaneCm.Y - CenterPlaneCm.Y) / CellSizeY / ShiftQuantumCells) * ShiftQuantumCells);
	if (ShiftCells == FIntPoint::ZeroValue)
	{
		return FIntPoint::ZeroValue;
	}

	CenterPlaneCm.X += static_cast<float>(ShiftCells.X) * CellSizeX;
	CenterPlaneCm.Y += static_cast<float>(ShiftCells.Y) * CellSizeY;
	PendingShiftCells += ShiftCells;
	ShiftPendingTerrainUpdates(ShiftCells);
	if (Settings.bUsesWaterLayer)
	{
		WaterFieldState.ShiftWindow(ShiftCells, CenterPlaneCm);
	}
	return ShiftCells;
}

void FTDFluidGpuSolverProxy::SetObstacles(TConstArrayView<FTDFluidSdfObstacle> PlaneObstacles)
{
	Obstacles.Reset(PlaneObstacles.Num());
	for (const FTDFluidSdfObstacle& PlaneObstacle : PlaneObstacles)
	{
		Obstacles.Add(MakeObstacleInput(PlaneObstacle));
	}
	bHasObstacleListChanged = true;
}

void FTDFluidGpuSolverProxy::SetTerrainElevationAtCell(int32 CellX, int32 CellY, float ElevationCm)
{
	if (Settings.bUsesWaterLayer)
	{
		if (!WaterFieldState.IsValidFieldCell(CellX, CellY))
		{
			return;
		}

		WaterFieldState.SetTerrainElevationAtFieldCell(CellX, CellY, ElevationCm);
		return;
	}

	if (!IsValidCell(CellX, CellY))
	{
		return;
	}

	FTDFluidGpuTerrainCellUpdate& Update = PendingTerrainUpdates.AddDefaulted_GetRef();
	Update.Cell = FIntPoint(CellX, CellY);
	Update.ElevationCm = ElevationCm;
}

void FTDFluidGpuSolverProxy::PrepareStep(float DeltaTime, TConstArrayView<FTDFluidBrushStamp> PlaneStamps, const FVector2D& PlaneUpDirection, bool bAllowsTerrainElevation)
{
	PreparedDeltaTimeSeconds = DeltaTime;
	PreparedPlaneUpDirection = PlaneUpDirection;
	bPreparedAllowsTerrainElevation = bAllowsTerrainElevation;
	PreparedStamps.Reserve(PreparedStamps.Num() + PlaneStamps.Num());
	for (const FTDFluidBrushStamp& PlaneStamp : PlaneStamps)
	{
		PreparedStamps.Add(MakeStampInput(PlaneStamp, Settings.bUsesWaterLayer));
	}
	if (Settings.bUsesWaterLayer)
	{
		WaterFieldState.RasterizeDirtyCells();
	}
	bHasPreparedStep = true;
}

bool FTDFluidGpuSolverProxy::GatherStep(
	uint32 FrameNumber,
	int32 TileIndex,
	FTextureRenderTargetResource* DensityPressureOutput,
	FTextureRenderTargetResource* VelocityOutput,
	FTDFluidGpuActorInput& OutActorInput,
	FTextureRenderTargetResource* AuxOutput,
	FTextureRenderTargetResource* TransmittanceOutput,
	FTextureRenderTargetResource* WaterSurfaceOutput,
	int32 WaterProbeFirstSlot)
{
	if (!bHasPreparedStep || !IsInitialized() || !HasActorKey())
	{
		return false;
	}

	OutActorInput.ActorKey = ActorKey;
	OutActorInput.Settings = Settings;
	OutActorInput.WindowCenterPlaneCm = CenterPlaneCm;
	OutActorInput.ShiftCells = PendingShiftCells;
	OutActorInput.PlaneUpDirection = PreparedPlaneUpDirection;
	OutActorInput.DeltaTimeSeconds = PreparedDeltaTimeSeconds;
	OutActorInput.SimulationTimeSeconds = SimulationTimeSeconds;
	OutActorInput.bShouldResetState = bNeedsReset;
	OutActorInput.bHasTerrainElevation = bPreparedAllowsTerrainElevation && bIsTerrainElevationActive;
	OutActorInput.bHasObstacleListChanged = bHasObstacleListChanged;
	OutActorInput.Stamps = MoveTemp(PreparedStamps);
	OutActorInput.Obstacles = Obstacles;
	if (OutActorInput.bHasTerrainElevation)
	{
		OutActorInput.TerrainElevationUpdates = MoveTemp(PendingTerrainUpdates);
	}
	OutActorInput.DensityPressureOutput = DensityPressureOutput;
	OutActorInput.VelocityOutput = VelocityOutput;
	OutActorInput.AuxOutput = Settings.bUsesHeatChannel ? AuxOutput : nullptr;
	OutActorInput.TransmittanceOutput = TransmittanceOutput;
	OutActorInput.Transmittance = PendingTransmittanceInput;

	int32 SubmittedWaterProbeCount = 0;
	if (Settings.bUsesWaterLayer)
	{
		CSV_SCOPED_TIMING_STAT(TDFluid, WaterGatherStep);
		SubmittedWaterProbeCount = GatherWaterStep(OutActorInput, WaterSurfaceOutput, WaterProbeFirstSlot);
	}

	RecordSubmission(FrameNumber, TileIndex, WaterProbeFirstSlot, SubmittedWaterProbeCount);
	AdvanceSimulationTime(PreparedDeltaTimeSeconds);
	PreparedStamps.Reset();
	PendingShiftCells = FIntPoint::ZeroValue;
	bNeedsReset = false;
	bHasObstacleListChanged = false;
	bHasPreparedStep = false;
	return true;
}

bool FTDFluidGpuSolverProxy::ConsumeReadbackFrame(const FTDFluidGpuReadbackFrame& ReadbackFrame)
{
	const int32 RecordIndex = RecentSubmissions.IndexOfByPredicate([&ReadbackFrame](const FTDFluidGpuSubmissionRecord& Record)
	{
		return Record.FrameNumber == ReadbackFrame.FrameNumber;
	});
	if (RecordIndex == INDEX_NONE)
	{
		return false;
	}

	const FTDFluidGpuSubmissionRecord Submission = RecentSubmissions[RecordIndex];
	RecentSubmissions.RemoveAt(0, RecordIndex + 1, EAllowShrinking::No);
	const bool bHasCopiedTile = ReadbackField.CopyFromAtlasTile(
		ReadbackFrame.Texels,
		ReadbackFrame.Layout.GetAtlasSize(),
		ReadbackFrame.Layout.GetTileOrigin(Submission.TileIndex),
		ReadbackFrame.FrameNumber,
		Submission.WindowCenterPlaneCm,
		Submission.ExtentCm);
	ConsumeWaterProbeSamples(ReadbackFrame, Submission);
	return bHasCopiedTile;
}

void FTDFluidGpuSolverProxy::AssignActorKey(uint64 InActorKey)
{
	ActorKey = InActorKey;
	if (Settings.bUsesWaterLayer)
	{
		WaterFieldState.RequestFullUpload(ProxyWaterFullUploadGatherCount);
	}
}

uint64 FTDFluidGpuSolverProxy::ReleaseActorKey()
{
	const uint64 ReleasedActorKey = ActorKey;
	ActorKey = 0;
	return ReleasedActorKey;
}

bool FTDFluidGpuSolverProxy::IsValidCell(int32 CellX, int32 CellY) const
{
	return CellX >= 0 && CellX < Resolution && CellY >= 0 && CellY < Resolution;
}

void FTDFluidGpuSolverProxy::ShiftPendingTerrainUpdates(const FIntPoint& ShiftCells)
{
	for (FTDFluidGpuTerrainCellUpdate& Update : PendingTerrainUpdates)
	{
		Update.Cell -= ShiftCells;
	}

	PendingTerrainUpdates.RemoveAll([this](const FTDFluidGpuTerrainCellUpdate& Update)
	{
		return !IsValidCell(Update.Cell.X, Update.Cell.Y);
	});
}

void FTDFluidGpuSolverProxy::RecordSubmission(uint32 FrameNumber, int32 TileIndex, int32 WaterProbeFirstSlot, int32 WaterProbeCount)
{
	if (RecentSubmissions.Num() >= MaximumRecentSubmissionCount)
	{
		RecentSubmissions.RemoveAt(0, 1, EAllowShrinking::No);
	}

	FTDFluidGpuSubmissionRecord& Submission = RecentSubmissions.AddDefaulted_GetRef();
	Submission.FrameNumber = FrameNumber;
	Submission.TileIndex = TileIndex;
	Submission.WindowCenterPlaneCm = CenterPlaneCm;
	Submission.ExtentCm = Settings.ExtentCm;
	Submission.WaterProbeFirstSlot = WaterProbeFirstSlot;
	Submission.WaterProbeCount = WaterProbeCount;
	Submission.WaterProbeGeneration = WaterProbeGeneration;
}

int32 FTDFluidGpuSolverProxy::GatherWaterStep(FTDFluidGpuActorInput& OutActorInput, FTextureRenderTargetResource* WaterSurfaceOutput, int32 WaterProbeFirstSlot)
{
	FTDFluidGpuBatch::ApplyWaterConsoleOverrides(OutActorInput.Settings);
	const FTDFluidGpuBatchKey GatheredKey = FTDFluidGpuBatch::MakeKey(OutActorInput.Settings);
	if (!bHasGatheredBatchKey || !(GatheredKey == LastGatheredBatchKey))
	{
		WaterFieldState.RequestFullUpload(ProxyWaterFullUploadGatherCount);
		LastGatheredBatchKey = GatheredKey;
		bHasGatheredBatchKey = true;
	}

	if (!FMath::IsFinite(PreparedDeltaTimeSeconds) || PreparedDeltaTimeSeconds <= 0.0f)
	{
		return 0;
	}

	WaterFieldState.MoveUpdatesInto(OutActorInput);
	OutActorInput.WaterTerrainReferenceZCm = WaterTerrainReferenceZCm;
	const float StepDeltaTime = FMath::Min(PreparedDeltaTimeSeconds, TDFluidGpuSolverConstants::MaximumStepDeltaTimeSeconds);
	const bool bUpdatesWetness = OutActorInput.Settings.bUsesWaterWetness && FTDFluidGrid2DSolver::IsWaterWetnessConsoleEnabled();
	OutActorInput.bUpdatesWaterWetness = bUpdatesWetness;
	OutActorInput.bResetsWaterWetness = bUpdatesWetness && !bWasUpdatingWaterWetness;
	bWasUpdatingWaterWetness = bUpdatesWetness;
	OutActorInput.WaterWetnessRetainFactor = FTDFluidWaterWetnessRules::ComputeRetainFactor(StepDeltaTime, OutActorInput.Settings.WetnessDryingSeconds);
	OutActorInput.WaterSwashReachCm = FTDFluidWaterWetnessRules::ComputeSwashReachCm(OutActorInput.Settings.SwashHeightCm, FTDFluidWaterWetnessRules::ComputeCoastWavePhase(SimulationTimeSeconds + StepDeltaTime, OutActorInput.Settings.CoastWavePeriodSeconds));
	OutActorInput.WaterSurfaceOutput = WaterSurfaceOutput;
	OutActorInput.WaterProbeFirstSlot = WaterProbeFirstSlot;

	const int32 ProbeSlotCapacity = FMath::Max(TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame - WaterProbeFirstSlot, 0);
	const int32 ProbeCount = FMath::Min(WaterProbePlanePoints.Num(), ProbeSlotCapacity);
	OutActorInput.WaterProbeCells.Reset(ProbeCount);
	for (int32 ProbeIndex = 0; ProbeIndex < ProbeCount; ++ProbeIndex)
	{
		OutActorInput.WaterProbeCells.Add(ProxyMapWaterProbePlanePointToCell(*this, WaterProbePlanePoints[ProbeIndex]));
	}
	return ProbeCount;
}

void FTDFluidGpuSolverProxy::ConsumeWaterProbeSamples(const FTDFluidGpuReadbackFrame& ReadbackFrame, const FTDFluidGpuSubmissionRecord& Submission)
{
	if (Submission.WaterProbeCount <= 0 || Submission.WaterProbeFirstSlot < 0)
	{
		return;
	}

	const int32 EndSlot = Submission.WaterProbeFirstSlot + Submission.WaterProbeCount;
	if (ReadbackFrame.WaterProbeSamples.Num() < EndSlot)
	{
		return;
	}

	LatestWaterProbeSamples.Reset(Submission.WaterProbeCount);
	LatestWaterProbeSamples.Append(ReadbackFrame.WaterProbeSamples.GetData() + Submission.WaterProbeFirstSlot, Submission.WaterProbeCount);
	LatestWaterProbeFrameNumber = ReadbackFrame.FrameNumber;
	LatestWaterProbeGeneration = Submission.WaterProbeGeneration;
}

void FTDFluidGpuSolverProxy::AdvanceSimulationTime(float DeltaTime)
{
	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return;
	}

	SimulationTimeSeconds += FMath::Min(DeltaTime, TDFluidGpuSolverConstants::MaximumStepDeltaTimeSeconds);
}

void FTDFluidGpuSolverRenderState::ExecuteFrame(FRHICommandListImmediate& RHICmdList, const FTDFluidGpuFrameInputs& FrameInputs)
{
	PublishReadyReadback();

	FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuSolverFrame"));
	const FTDFluidGpuFrameOutputs FrameOutputs = AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, Batches);
	ReadbackRing.Enqueue(GraphBuilder, FrameOutputs);
	GraphBuilder.Execute();
}

bool FTDFluidGpuSolverRenderState::TakeLatestReadback(FTDFluidGpuReadbackFrame& OutReadbackFrame)
{
	FScopeLock ReadbackLock(&ReadbackMutex);
	if (!bHasUnreadReadback)
	{
		return false;
	}

	OutReadbackFrame = MoveTemp(LatestReadback);
	LatestReadback = FTDFluidGpuReadbackFrame();
	bHasUnreadReadback = false;
	return true;
}

void FTDFluidGpuSolverRenderState::PublishReadyReadback()
{
	FTDFluidGpuReadbackFrame ReadyFrame;
	if (!ReadbackRing.TryReadLatest(ReadyFrame))
	{
		return;
	}

	FScopeLock ReadbackLock(&ReadbackMutex);
	LatestReadback = MoveTemp(ReadyFrame);
	bHasUnreadReadback = true;
}
