#include "TDFluidGpuBatch.h"

#include "HAL/IConsoleManager.h"

namespace
{
	constexpr int32 SliceCapacityGranularity = 4;
	constexpr int32 ExactSliceCapacityMinimumResolution = 512;
	constexpr double FallbackExtentCm = 4000.0;
	constexpr double MinimumExtentCm = 100.0;
	constexpr float AdvectVelocityDampingScale = 15.0f;
	constexpr float PainterVelocityDampingScale = 10.0f;

	constexpr int64 HalfFloatBytes = 2;
	constexpr int64 FloatBytes = 4;
	constexpr int64 PingPongTextureCount = 2;
	constexpr int64 RgbaHalfFloatBytes = 4 * HalfFloatBytes;
	constexpr int64 VelocityBytesPerCell = PingPongTextureCount * 2 * HalfFloatBytes;
	constexpr int64 DensityBytesPerCell = PingPongTextureCount * 2 * FloatBytes;
	constexpr int64 SurfaceDisplacementBytesPerCell = PingPongTextureCount * HalfFloatBytes;
	constexpr int64 PressureSolutionBytesPerCell = PingPongTextureCount * FloatBytes;
	constexpr int64 ObstacleSdfBytesPerCell = HalfFloatBytes;
	constexpr int64 TerrainElevationBytesPerCell = FloatBytes;
	constexpr int64 DivergenceCurlBytesPerCell = HalfFloatBytes;
	constexpr int64 FluidOutputBytesPerCell = 2 * RgbaHalfFloatBytes;
	constexpr int64 PainterStateBytesPerCell = 2 * FloatBytes;
	constexpr int64 PainterOutputBytesPerCell = RgbaHalfFloatBytes;
	constexpr int64 PainterVelocityOutputBytes = 4 * 4 * RgbaHalfFloatBytes;
	constexpr int64 MultigridRhsBytesPerCell = FloatBytes;
	constexpr int64 MultigridCorrectionBytesPerCell = FloatBytes;
	constexpr int64 MultigridApertureBytesPerCell = 2 * HalfFloatBytes;
	constexpr int64 MultigridComponentLabelBytesPerCell = 4;
	constexpr int64 HeatBytesPerCell = PingPongTextureCount * FloatBytes;
	constexpr int64 AuxOutputBytesPerCell = RgbaHalfFloatBytes;
	constexpr int32 ForcedJacobiSolverMode = 1;
	constexpr int32 ForcedMultigridSolverMode = 2;

	TAutoConsoleVariable<int32> CVarTDFluidGpuPressureSolver(
		TEXT("TD.FluidFX.GpuPressureSolver"),
		0,
		TEXT("GPU fluid pressure solver. 0: automatic (multigrid V cycles at the 512 tier and above, Jacobi below), 1: Jacobi everywhere, 2: multigrid at the 64 tier and above. Changing it moves GPU fluid actors into new batches and resets their state."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarTDFluidGpuPressureCycles(
		TEXT("TD.FluidFX.GpuPressureCycles"),
		TDFluidGpuSolverConstants::DefaultMultigridCycles,
		TEXT("Multigrid V cycles per GPU fluid pressure projection, clamped to 1..4. Changing it resets multigrid GPU fluid actors."),
		ECVF_RenderThreadSafe);

	struct FTDFluidGpuNoiseLatticePlacement
	{
		FIntPoint Origin = FIntPoint::ZeroValue;
		FVector2f Offset = FVector2f::ZeroVector;
		FVector2f StepPerCell = FVector2f::ZeroVector;
	};

	struct FTDFluidGpuNoiseTime
	{
		uint32 WholePart = 0;
		float FractionPart = 0.0f;
	};

	float ClampFinite(float Value, float MinValue, float MaxValue)
	{
		if (!FMath::IsFinite(Value))
		{
			return MinValue;
		}

		return FMath::Clamp(Value, MinValue, MaxValue);
	}

	double ClampExtentCm(double Value)
	{
		if (!FMath::IsFinite(Value))
		{
			return FallbackExtentCm;
		}

		return FMath::Max(Value, MinimumExtentCm);
	}

	bool IsSteppableDeltaTime(float DeltaTimeSeconds)
	{
		return FMath::IsFinite(DeltaTimeSeconds) && DeltaTimeSeconds > 0.0f;
	}

	FTDFluidGpuSimulationSettings SanitizeSettings(const FTDFluidGpuSimulationSettings& Settings)
	{
		FTDFluidGpuSimulationSettings Sanitized = Settings;
		Sanitized.Resolution = FTDFluidGpuBatch::ResolveResolutionTier(Settings.Resolution);
		Sanitized.ExtentCm.X = ClampExtentCm(Settings.ExtentCm.X);
		Sanitized.ExtentCm.Y = ClampExtentCm(Settings.ExtentCm.Y);
		Sanitized.VelocityDamping = ClampFinite(Settings.VelocityDamping, 0.0f, 10.0f);
		Sanitized.DensityDissipation = ClampFinite(Settings.DensityDissipation, 0.0f, 10.0f);
		Sanitized.PressureIterations = FMath::Clamp(Settings.PressureIterations, TDFluidGpuSolverConstants::MinimumPressureIterations, TDFluidGpuSolverConstants::MaximumPressureIterations);
		Sanitized.VorticityStrength = ClampFinite(Settings.VorticityStrength, 0.0f, 20.0f);
		Sanitized.TerrainSlopeGravity = ClampFinite(Settings.TerrainSlopeGravity, 0.0f, 1000000.0f);
		Sanitized.AdvectionSpeedScale = ClampFinite(Settings.AdvectionSpeedScale, 0.0f, 10.0f);
		Sanitized.BorderFadeRatio = ClampFinite(Settings.BorderFadeRatio, 0.0f, 0.49f);
		Sanitized.MaxStampsPerTick = FMath::Clamp(Settings.MaxStampsPerTick, 1, TDFluidGpuSolverConstants::MaximumStampsPerTick);
		Sanitized.AmbientDensity = ClampFinite(Settings.AmbientDensity, 0.0f, 1.0f);
		Sanitized.AmbientRefillPerSec = ClampFinite(Settings.AmbientRefillPerSec, 0.0f, 100.0f);
		Sanitized.FlowNoiseAccelerationCmPerSec2 = ClampFinite(Settings.FlowNoiseAccelerationCmPerSec2, 0.0f, 10000.0f);
		Sanitized.FlowNoiseScaleCm = ClampFinite(Settings.FlowNoiseScaleCm, 1.0f, 100000.0f);
		Sanitized.FlowNoiseSpeed = ClampFinite(Settings.FlowNoiseSpeed, 0.0f, 100.0f);
		Sanitized.AmbientNoiseAmount = ClampFinite(Settings.AmbientNoiseAmount, 0.0f, 1.0f);
		Sanitized.BuoyancyCmPerSec2 = ClampFinite(Settings.BuoyancyCmPerSec2, 0.0f, 20000.0f);
		Sanitized.OutflowBandRatio = ClampFinite(Settings.OutflowBandRatio, 0.0f, 0.45f);
		Sanitized.OutflowRatePerSec = ClampFinite(Settings.OutflowRatePerSec, 0.0f, 50.0f);
		Sanitized.HeatCoolingPerSec = ClampFinite(Settings.HeatCoolingPerSec, 0.0f, 50.0f);
		Sanitized.HeatBuoyancyCmPerSec2 = ClampFinite(Settings.HeatBuoyancyCmPerSec2, 0.0f, 20000.0f);
		Sanitized.EdgeBounciness = ClampFinite(Settings.EdgeBounciness, 0.0f, 1.0f);
		Sanitized.BorderFadeExponent = FMath::IsFinite(Settings.BorderFadeExponent) ? FMath::Clamp(Settings.BorderFadeExponent, 0.25f, 8.0f) : 1.0f;
		Sanitized.VortexTangentialAccelerationCmPerSec2 = FMath::IsFinite(Settings.VortexTangentialAccelerationCmPerSec2) ? FMath::Clamp(Settings.VortexTangentialAccelerationCmPerSec2, -20000.0f, 20000.0f) : 0.0f;
		Sanitized.VortexRadialDriftCmPerSec = FMath::IsFinite(Settings.VortexRadialDriftCmPerSec) ? FMath::Clamp(Settings.VortexRadialDriftCmPerSec, -TDFluidGpuSolverConstants::MaxCellSpeedCmPerSec, TDFluidGpuSolverConstants::MaxCellSpeedCmPerSec) : 0.0f;
		Sanitized.VortexRadiusRatio = ClampFinite(Settings.VortexRadiusRatio, 0.01f, 0.5f);
		Sanitized.bUsesHeatChannel = Settings.bUsesHeatChannel && Settings.SimMode == ETDFluidGpuSimMode::FluidSim;
		return Sanitized;
	}

	FVector2D ResolvePlaneUpDirection(const FVector2D& PlaneUpDirection)
	{
		if (PlaneUpDirection.ContainsNaN() || PlaneUpDirection.IsNearlyZero())
		{
			return FVector2D::ZeroVector;
		}

		return PlaneUpDirection.GetSafeNormal();
	}

	FTDFluidGpuNoiseLatticePlacement MakeNoiseLatticePlacement(const FTDFluidGpuSimulationSettings& Settings, const FVector2D& WindowCenter, int32 Resolution, float ScaleMultiplier)
	{
		const float LargestCellSizeCm = static_cast<float>(FMath::Max(Settings.ExtentCm.X, Settings.ExtentCm.Y)) / static_cast<float>(Resolution);
		const float SpacingCm = FMath::Max(Settings.FlowNoiseScaleCm, LargestCellSizeCm) * ScaleMultiplier;
		const double InverseSpacing = 1.0 / static_cast<double>(SpacingCm);
		const double LatticeStartX = (WindowCenter.X - 0.5 * Settings.ExtentCm.X) * InverseSpacing;
		const double LatticeStartY = (WindowCenter.Y - 0.5 * Settings.ExtentCm.Y) * InverseSpacing;
		const double OriginX = FMath::FloorToDouble(LatticeStartX);
		const double OriginY = FMath::FloorToDouble(LatticeStartY);

		FTDFluidGpuNoiseLatticePlacement Placement;
		Placement.Origin = FIntPoint(static_cast<int32>(OriginX), static_cast<int32>(OriginY));
		Placement.Offset = FVector2f(static_cast<float>(LatticeStartX - OriginX), static_cast<float>(LatticeStartY - OriginY));
		Placement.StepPerCell = FVector2f(
			static_cast<float>(Settings.ExtentCm.X / Resolution * InverseSpacing),
			static_cast<float>(Settings.ExtentCm.Y / Resolution * InverseSpacing));
		return Placement;
	}

	FTDFluidGpuNoiseTime SplitNoiseTime(const FTDFluidGpuSimulationSettings& Settings, double SimulationTimeSeconds)
	{
		const double NoiseTime = static_cast<double>(Settings.FlowNoiseSpeed) * SimulationTimeSeconds;
		const double SafeNoiseTime = FMath::IsFinite(NoiseTime) && NoiseTime > 0.0 ? NoiseTime : 0.0;
		const double WholePart = FMath::FloorToDouble(SafeNoiseTime);

		FTDFluidGpuNoiseTime SplitTime;
		SplitTime.WholePart = static_cast<uint32>(FMath::Fmod(WholePart, TDFluidGpuSolverConstants::NoiseTimeWrapPeriod));
		SplitTime.FractionPart = static_cast<float>(SafeNoiseTime - WholePart);
		return SplitTime;
	}

	FTDFluidGpuActorParameters MakeActorParameters(const FTDFluidGpuActorInput& Actor, const FTDFluidGpuSimulationSettings& Settings, const FVector2D& WindowCenter, const FVector2D& PlaneUpDirection, int32 Resolution)
	{
		const float ResolutionAsFloat = static_cast<float>(Resolution);
		const float CellSizeX = static_cast<float>(Settings.ExtentCm.X / ResolutionAsFloat);
		const float CellSizeY = static_cast<float>(Settings.ExtentCm.Y / ResolutionAsFloat);
		const float SmallestCellSize = FMath::Min(CellSizeX, CellSizeY);
		const float SpacingX = CellSizeX / SmallestCellSize;
		const float SpacingY = CellSizeY / SmallestCellSize;
		const float InvSpacingXSquared = 1.0f / (SpacingX * SpacingX);
		const float InvSpacingYSquared = 1.0f / (SpacingY * SpacingY);
		const float StampDeltaTime = Actor.DeltaTimeSeconds;
		const float StepDeltaTime = FMath::Min(Actor.DeltaTimeSeconds, TDFluidGpuSolverConstants::MaximumStepDeltaTimeSeconds);
		const float SmallestExtentCm = static_cast<float>(FMath::Min(Settings.ExtentCm.X, Settings.ExtentCm.Y));
		const float LargestExtentCm = static_cast<float>(FMath::Max(Settings.ExtentCm.X, Settings.ExtentCm.Y));
		const FTDFluidGpuNoiseLatticePlacement FlowNoiseLattice = MakeNoiseLatticePlacement(Settings, WindowCenter, Resolution, TDFluidGpuSolverConstants::FlowNoiseScaleMultiplier);
		const FTDFluidGpuNoiseLatticePlacement AmbientNoiseLattice = MakeNoiseLatticePlacement(Settings, WindowCenter, Resolution, TDFluidGpuSolverConstants::AmbientNoiseScaleMultiplier);
		const FTDFluidGpuNoiseTime NoiseTime = SplitNoiseTime(Settings, Actor.SimulationTimeSeconds);

		FTDFluidGpuActorParameters Parameters;
		Parameters.Resolution = Resolution;
		Parameters.PressureIterations = static_cast<uint32>(Settings.PressureIterations);
		Parameters.ExtentCm = FVector2f(Settings.ExtentCm);
		Parameters.CellSizeCm = FVector2f(CellSizeX, CellSizeY);
		Parameters.RelativeCellSpacing = FVector2f(SpacingX, SpacingY);
		Parameters.InvSpacingXSquared = InvSpacingXSquared;
		Parameters.InvSpacingYSquared = InvSpacingYSquared;
		Parameters.InvNeighborWeightSum = 1.0f / (2.0f * (InvSpacingXSquared + InvSpacingYSquared));
		Parameters.PressureDisplayScale = (SmallestExtentCm / ResolutionAsFloat) / (TDFluidGpuSolverConstants::PressureNormalizationCmPerSec * TDFluidGpuSolverConstants::PressureReferenceCellSizeCm);
		Parameters.ObstacleBoundaryBandCm = (LargestExtentCm / ResolutionAsFloat) * TDFluidGpuSolverConstants::ObstacleBoundaryBandCells;
		Parameters.BorderFadeRatio = Settings.BorderFadeRatio;
		Parameters.StampDeltaTimeSeconds = StampDeltaTime;
		Parameters.StepDeltaTimeSeconds = StepDeltaTime;
		Parameters.StampVelocityBlend = FMath::Min(1.0f, TDFluidGpuSolverConstants::BrushVelocityResponsePerSec * StampDeltaTime);
		Parameters.VorticityStrength = Settings.VorticityStrength;
		Parameters.AdvectionStepScale = FVector2f((StepDeltaTime * Settings.AdvectionSpeedScale) / CellSizeX, (StepDeltaTime * Settings.AdvectionSpeedScale) / CellSizeY);
		Parameters.AdvectVelocityDampingFactor = 1.0f / (1.0f + Settings.VelocityDamping * StepDeltaTime * AdvectVelocityDampingScale);
		Parameters.DensityDissipationFactor = 1.0f / (1.0f + Settings.DensityDissipation * StepDeltaTime);
		Parameters.PainterVelocityDecay = 1.0f / (1.0f + Settings.VelocityDamping * StepDeltaTime * PainterVelocityDampingScale);
		Parameters.TerrainSlopeGravity = Settings.TerrainSlopeGravity;
		Parameters.FlowNoiseAccelerationCmPerSec2 = Settings.FlowNoiseAccelerationCmPerSec2;
		Parameters.AmbientNoiseAmount = Settings.AmbientNoiseAmount;
		Parameters.FlowNoiseLatticeOrigin = FlowNoiseLattice.Origin;
		Parameters.FlowNoiseLatticeOffset = FlowNoiseLattice.Offset;
		Parameters.FlowNoiseLatticeStepPerCell = FlowNoiseLattice.StepPerCell;
		Parameters.AmbientNoiseLatticeStepPerCell = AmbientNoiseLattice.StepPerCell;
		Parameters.AmbientNoiseLatticeOrigin = AmbientNoiseLattice.Origin;
		Parameters.AmbientNoiseLatticeOffset = AmbientNoiseLattice.Offset;
		Parameters.NoiseTimeWholePart = NoiseTime.WholePart;
		Parameters.NoiseTimeFractionPart = NoiseTime.FractionPart;
		Parameters.AmbientDensity = Settings.AmbientDensity;
		Parameters.AmbientRefillBlend = 1.0f - FMath::Exp(-Settings.AmbientRefillPerSec * StepDeltaTime);
		Parameters.BuoyancyVelocityStepCmPerSec = FVector2f(
			static_cast<float>(PlaneUpDirection.X) * Settings.BuoyancyCmPerSec2 * StepDeltaTime,
			static_cast<float>(PlaneUpDirection.Y) * Settings.BuoyancyCmPerSec2 * StepDeltaTime);
		Parameters.PlaneUpDirection = FVector2f(PlaneUpDirection);
		Parameters.OutflowBandWidth = 2.0f * Settings.OutflowBandRatio;
		Parameters.OutflowBandStart = 1.0f - Parameters.OutflowBandWidth;
		Parameters.OutflowRatePerSec = Settings.OutflowRatePerSec;
		Parameters.StampBinSpanCells = static_cast<uint32>(FTDFluidGpuStampBins::GetBinSpanCells(Resolution));
		Parameters.HeatDissipationFactor = 1.0f / (1.0f + Settings.HeatCoolingPerSec * StepDeltaTime);
		Parameters.HeatBuoyancyVelocityStepCmPerSec = FVector2f(
			static_cast<float>(PlaneUpDirection.X) * Settings.HeatBuoyancyCmPerSec2 * StepDeltaTime,
			static_cast<float>(PlaneUpDirection.Y) * Settings.HeatBuoyancyCmPerSec2 * StepDeltaTime);
		Parameters.EdgeBounciness = Settings.EdgeBounciness;
		Parameters.BorderFadeExponent = Settings.BorderFadeExponent;
		Parameters.VortexTangentialStepCmPerSec = Settings.VortexTangentialAccelerationCmPerSec2 * StepDeltaTime;
		Parameters.VortexRadialDriftCmPerSec = Settings.VortexRadialDriftCmPerSec;
		Parameters.VortexRadiusCm = Settings.VortexRadiusRatio * SmallestExtentCm;
		return Parameters;
	}

	void AddFlagIf(ETDFluidGpuActorFlags& InOutFlags, ETDFluidGpuActorFlags Flag, bool bCondition)
	{
		if (!bCondition)
		{
			return;
		}

		InOutFlags |= Flag;
	}

	ETDFluidGpuActorFlags ResolveActorFlags(const FTDFluidGpuActorInput& Actor, const FTDFluidGpuSimulationSettings& Settings, const FVector2D& PlaneUpDirection, bool bNeedsReset, int32 StampCount, int32 ObstacleCount)
	{
		const bool bIsSimplePainter = Settings.SimMode == ETDFluidGpuSimMode::SimplePainter;
		const bool bRunsFluidForces = !bIsSimplePainter;
		const bool bHasPlaneUp = !PlaneUpDirection.IsNearlyZero();
		const bool bHasShift = !bNeedsReset && Actor.ShiftCells != FIntPoint::ZeroValue;
		const bool bHasObstacles = ObstacleCount > 0;
		const bool bHasTerrainElevation = bRunsFluidForces && Actor.bHasTerrainElevation;
		const bool bUsesAmbientRefill = Settings.AmbientRefillPerSec > 0.0f;

		ETDFluidGpuActorFlags Flags = ETDFluidGpuActorFlags::None;
		AddFlagIf(Flags, ETDFluidGpuActorFlags::IsSimplePainter, bIsSimplePainter);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::NeedsReset, bNeedsReset);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::HasShift, bHasShift);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::RasterizesObstacles, Actor.bHasObstacleListChanged || ((bHasShift || bNeedsReset) && bHasObstacles));
		AddFlagIf(Flags, ETDFluidGpuActorFlags::HasObstacles, bHasObstacles);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::HasTerrainElevation, bHasTerrainElevation);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesTerrainSlopeAcceleration, bHasTerrainElevation && Settings.TerrainSlopeGravity > 0.0f);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesFlowNoise, bRunsFluidForces && Settings.FlowNoiseAccelerationCmPerSec2 > 0.0f);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesBuoyancy, bRunsFluidForces && bHasPlaneUp && Settings.BuoyancyCmPerSec2 > 0.0f);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesVorticity, bRunsFluidForces && Settings.VorticityStrength > 0.0f);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesOutflow, bRunsFluidForces && bHasPlaneUp && Settings.OutflowRatePerSec > 0.0f && Settings.OutflowBandRatio > 0.0f);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesAmbientRefill, bUsesAmbientRefill);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesAmbientNoise, bUsesAmbientRefill && Settings.AmbientNoiseAmount > 0.0f);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::HasStamps, StampCount > 0);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesHeatChannel, bRunsFluidForces && Settings.bUsesHeatChannel);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesBoundaryReflection, bRunsFluidForces && Settings.bReflectsAtBoundaries);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesVortexForce, bRunsFluidForces && Settings.VortexRadiusRatio > 0.0f && (Settings.VortexTangentialAccelerationCmPerSec2 != 0.0f || Settings.VortexRadialDriftCmPerSec != 0.0f));
		AddFlagIf(Flags, ETDFluidGpuActorFlags::WritesAuxOutput, bRunsFluidForces && Settings.bUsesHeatChannel && Actor.AuxOutput != nullptr);
		return Flags;
	}

	void AppendPackedObstacles(TConstArrayView<FTDFluidGpuObstacleInput> Obstacles, const FVector2D& WindowCenter, TArray<FTDFluidGpuPackedObstacle>& InOutObstacles)
	{
		for (const FTDFluidGpuObstacleInput& Obstacle : Obstacles)
		{
			const bool bIsFinite = !Obstacle.CenterPlaneCm.ContainsNaN() && !Obstacle.BoxHalfExtentsCm.ContainsNaN() && FMath::IsFinite(Obstacle.RadiusCm);
			if (!bIsFinite)
			{
				continue;
			}

			FTDFluidGpuPackedObstacle& Packed = InOutObstacles.AddDefaulted_GetRef();
			Packed.CenterFromWindowCenterCm = FVector2f(Obstacle.CenterPlaneCm - WindowCenter);
			Packed.RadiusCm = FMath::Max(Obstacle.RadiusCm, 1.0f);
			Packed.bIsBox = Obstacle.bIsBox ? 1u : 0u;
			Packed.BoxHalfExtentsCm = FVector2f(Obstacle.BoxHalfExtentsCm);
		}
	}

	void AppendPackedTerrainUpdates(TConstArrayView<FTDFluidGpuTerrainCellUpdate> Updates, int32 SliceIndex, int32 Resolution, TArray<FTDFluidGpuPackedTerrainUpdate>& InOutUpdates)
	{
		for (const FTDFluidGpuTerrainCellUpdate& Update : Updates)
		{
			const bool bIsInsideGrid = Update.Cell.X >= 0 && Update.Cell.X < Resolution && Update.Cell.Y >= 0 && Update.Cell.Y < Resolution;
			if (!bIsInsideGrid || !FMath::IsFinite(Update.ElevationCm))
			{
				continue;
			}

			FTDFluidGpuPackedTerrainUpdate& Packed = InOutUpdates.AddDefaulted_GetRef();
			Packed.SliceIndex = static_cast<uint32>(SliceIndex);
			Packed.CellX = static_cast<uint32>(Update.Cell.X);
			Packed.CellY = static_cast<uint32>(Update.Cell.Y);
			Packed.ElevationCm = Update.ElevationCm;
		}
	}

	FTDFluidGpuBatch& FindOrAddBatch(TArray<FTDFluidGpuBatch>& InOutBatches, const FTDFluidGpuBatchKey& Key)
	{
		for (FTDFluidGpuBatch& Batch : InOutBatches)
		{
			if (Batch.GetKey() == Key)
			{
				return Batch;
			}
		}

		return InOutBatches.Emplace_GetRef(Key);
	}
}

FTDFluidGpuBatch::FTDFluidGpuBatch(const FTDFluidGpuBatchKey& InKey)
	: Key(InKey)
{
}

int32 FTDFluidGpuBatch::ResolveResolutionTier(int32 RequestedResolution)
{
	const int32 ClampedResolution = FMath::Clamp(RequestedResolution, TDFluidGpuSolverConstants::MinimumResolution, TDFluidGpuSolverConstants::MaximumResolution);
	return static_cast<int32>(FMath::RoundUpToPowerOfTwo(static_cast<uint32>(ClampedResolution)));
}

FTDFluidGpuBatchKey FTDFluidGpuBatch::MakeKey(const FTDFluidGpuSimulationSettings& Settings)
{
	FTDFluidGpuBatchKey BatchKey;
	BatchKey.SimMode = Settings.SimMode;
	BatchKey.ResolutionTier = ResolveResolutionTier(Settings.Resolution);
	if (Settings.SimMode == ETDFluidGpuSimMode::SimplePainter)
	{
		return BatchKey;
	}

	BatchKey.bUsesHeatChannel = Settings.bUsesHeatChannel;
	BatchKey.PressureSolver = ResolvePressureSolver(BatchKey.ResolutionTier);
	if (BatchKey.PressureSolver == ETDFluidGpuPressureSolver::Multigrid)
	{
		BatchKey.MultigridCycles = FMath::Clamp(CVarTDFluidGpuPressureCycles.GetValueOnAnyThread(), TDFluidGpuSolverConstants::MinimumMultigridCycles, TDFluidGpuSolverConstants::MaximumMultigridCycles);
		return BatchKey;
	}

	BatchKey.PressureIterations = FMath::Clamp(Settings.PressureIterations, TDFluidGpuSolverConstants::MinimumPressureIterations, TDFluidGpuSolverConstants::MaximumPressureIterations);
	return BatchKey;
}

ETDFluidGpuPressureSolver FTDFluidGpuBatch::ResolvePressureSolver(int32 ResolutionTier)
{
	const int32 SolverMode = CVarTDFluidGpuPressureSolver.GetValueOnAnyThread();
	if (SolverMode == ForcedJacobiSolverMode)
	{
		return ETDFluidGpuPressureSolver::Jacobi;
	}

	const int32 MinimumResolution = SolverMode == ForcedMultigridSolverMode ? TDFluidGpuSolverConstants::MultigridMinimumResolution : TDFluidGpuSolverConstants::MultigridAutoMinimumResolution;
	return ResolutionTier >= MinimumResolution ? ETDFluidGpuPressureSolver::Multigrid : ETDFluidGpuPressureSolver::Jacobi;
}

int32 FTDFluidGpuBatch::ComputeMultigridLevelCount(int32 ResolutionTier)
{
	int32 LevelCount = 1;
	for (int32 LevelResolution = ResolutionTier; LevelResolution > TDFluidGpuSolverConstants::MultigridCoarsestResolution; LevelResolution /= 2)
	{
		++LevelCount;
	}
	return LevelCount;
}

int64 FTDFluidGpuBatch::ComputeMultigridTransientBytes(int32 ResolutionTier)
{
	int64 TotalBytes = 0;
	const int32 LevelCount = ComputeMultigridLevelCount(ResolutionTier);
	for (int32 Level = 1; Level < LevelCount; ++Level)
	{
		const int64 LevelResolution = ResolutionTier >> Level;
		const int64 CorrectionTextureCount = Level == LevelCount - 1 ? 1 : 2;
		const int64 ComponentLabelBytesPerCell = Level == LevelCount - 1 ? MultigridComponentLabelBytesPerCell : 0;
		TotalBytes += LevelResolution * LevelResolution * (MultigridRhsBytesPerCell + CorrectionTextureCount * MultigridCorrectionBytesPerCell + MultigridApertureBytesPerCell + ComponentLabelBytesPerCell);
	}
	return TotalBytes;
}

int32 FTDFluidGpuBatch::RoundUpSliceCapacity(int32 SliceCount)
{
	if (SliceCount <= 0)
	{
		return 0;
	}

	return FMath::DivideAndRoundUp(SliceCount, SliceCapacityGranularity) * SliceCapacityGranularity;
}

int32 FTDFluidGpuBatch::ComputeTransmittanceResolution(int32 ResolutionTier)
{
	return FMath::Max(TDFluidGpuSolverConstants::MinimumTransmittanceResolution, FMath::DivideAndRoundUp(FMath::Max(ResolutionTier, 1), TDFluidGpuSolverConstants::TransmittanceResolutionDivisor));
}

FTDFluidGpuMemoryEstimate FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode SimMode, int32 RequestedResolution, bool bUsesHeatChannel, bool bUsesTransmittance)
{
	const int64 Resolution = ResolveResolutionTier(RequestedResolution);
	const int64 CellCount = Resolution * Resolution;
	const int64 ReadbackTileTexelCount = static_cast<int64>(TDFluidGpuSolverConstants::ReadbackTileResolution) * TDFluidGpuSolverConstants::ReadbackTileResolution;

	FTDFluidGpuMemoryEstimate Estimate;
	Estimate.ReadbackBytes = ReadbackTileTexelCount * RgbaHalfFloatBytes * TDFluidGpuSolverConstants::ReadbackRingSize;
	if (SimMode == ETDFluidGpuSimMode::SimplePainter)
	{
		Estimate.StateBytes = CellCount * PainterStateBytesPerCell;
		Estimate.OutputBytes = CellCount * PainterOutputBytesPerCell + PainterVelocityOutputBytes;
		return Estimate;
	}

	Estimate.StateBytes = CellCount * (VelocityBytesPerCell + DensityBytesPerCell + SurfaceDisplacementBytesPerCell + PressureSolutionBytesPerCell + ObstacleSdfBytesPerCell + TerrainElevationBytesPerCell);
	Estimate.TransientBytes = ResolvePressureSolver(static_cast<int32>(Resolution)) == ETDFluidGpuPressureSolver::Multigrid
		? ComputeMultigridTransientBytes(static_cast<int32>(Resolution))
		: CellCount * DivergenceCurlBytesPerCell;
	Estimate.OutputBytes = CellCount * FluidOutputBytesPerCell;
	if (bUsesHeatChannel)
	{
		Estimate.StateBytes += CellCount * HeatBytesPerCell;
		Estimate.OutputBytes += CellCount * AuxOutputBytesPerCell;
	}
	if (bUsesTransmittance)
	{
		const int64 TransmittanceResolution = ComputeTransmittanceResolution(static_cast<int32>(Resolution));
		Estimate.OutputBytes += TransmittanceResolution * TransmittanceResolution * RgbaHalfFloatBytes;
	}
	return Estimate;
}

void FTDFluidGpuBatch::PrepareFrame(const FTDFluidGpuFrameInputs& FrameInputs, TArray<FTDFluidGpuBatch>& InOutBatches, FTDFluidGpuPreparedFrame& OutPreparedFrame)
{
	OutPreparedFrame = FTDFluidGpuPreparedFrame();
	OutPreparedFrame.ReadbackLayout = FTDFluidGpuReadbackAtlasLayout::Make(FrameInputs.Actors.Num());

	for (const uint64 ReleasedActorKey : FrameInputs.ReleasedActorKeys)
	{
		ReleaseActorFromBatches(InOutBatches, ReleasedActorKey, nullptr);
	}

	for (FTDFluidGpuBatch& Batch : InOutBatches)
	{
		Batch.Frame = FTDFluidGpuBatchFrame();
	}

	TSet<uint64> ScheduledActorKeys;
	ScheduledActorKeys.Reserve(FrameInputs.Actors.Num());
	for (int32 ActorInputIndex = 0; ActorInputIndex < FrameInputs.Actors.Num(); ++ActorInputIndex)
	{
		const FTDFluidGpuActorInput& Actor = FrameInputs.Actors[ActorInputIndex];
		bool bIsAlreadyScheduled = false;
		ScheduledActorKeys.Add(Actor.ActorKey, &bIsAlreadyScheduled);
		if (bIsAlreadyScheduled)
		{
			continue;
		}

		const FTDFluidGpuBatchKey ActorBatchKey = MakeKey(Actor.Settings);
		ReleaseActorFromBatches(InOutBatches, Actor.ActorKey, &ActorBatchKey);
		FTDFluidGpuBatch& Batch = FindOrAddBatch(InOutBatches, ActorBatchKey);
		const int32 SliceIndex = Batch.AcquireSlice(Actor.ActorKey);
		if (!IsSteppableDeltaTime(Actor.DeltaTimeSeconds))
		{
			continue;
		}

		Batch.AddFrameEntry(ActorInputIndex, SliceIndex, Actor.bShouldResetState);
	}

	InOutBatches.RemoveAll([](const FTDFluidGpuBatch& Batch)
	{
		return Batch.GetAssignedSliceCount() == 0;
	});

	for (FTDFluidGpuBatch& Batch : InOutBatches)
	{
		Batch.BuildFrameParameters(FrameInputs, OutPreparedFrame);
	}
}

int32 FTDFluidGpuBatch::FindSlice(uint64 ActorKey) const
{
	const int32* SliceIndex = SliceByActorKey.Find(ActorKey);
	return SliceIndex ? *SliceIndex : INDEX_NONE;
}

int32 FTDFluidGpuBatch::GetRequiredSliceCapacity() const
{
	const int32 HighestOccupiedSlice = SliceOccupancy.FindLast(true);
	if (Key.ResolutionTier >= ExactSliceCapacityMinimumResolution)
	{
		return HighestOccupiedSlice + 1;
	}

	return RoundUpSliceCapacity(HighestOccupiedSlice + 1);
}

void FTDFluidGpuBatch::ReleaseActorFromBatches(TArray<FTDFluidGpuBatch>& InOutBatches, uint64 ActorKey, const FTDFluidGpuBatchKey* KeptBatchKey)
{
	for (FTDFluidGpuBatch& Batch : InOutBatches)
	{
		if (KeptBatchKey && Batch.Key == *KeptBatchKey)
		{
			continue;
		}

		Batch.ReleaseSlice(ActorKey);
	}
}

int32 FTDFluidGpuBatch::AcquireSlice(uint64 ActorKey)
{
	if (const int32* ExistingSlice = SliceByActorKey.Find(ActorKey))
	{
		return *ExistingSlice;
	}

	int32 SliceIndex = SliceOccupancy.Find(false);
	if (SliceIndex == INDEX_NONE)
	{
		SliceIndex = SliceOccupancy.Add(false);
		SlicePendingReset.Add(false);
	}

	SliceOccupancy[SliceIndex] = true;
	SlicePendingReset[SliceIndex] = true;
	SliceByActorKey.Add(ActorKey, SliceIndex);
	return SliceIndex;
}

bool FTDFluidGpuBatch::ReleaseSlice(uint64 ActorKey)
{
	int32 SliceIndex = INDEX_NONE;
	if (!SliceByActorKey.RemoveAndCopyValue(ActorKey, SliceIndex))
	{
		return false;
	}

	SliceOccupancy[SliceIndex] = false;
	SlicePendingReset[SliceIndex] = false;
	return true;
}

bool FTDFluidGpuBatch::ConsumePendingReset(int32 SliceIndex)
{
	const bool bWasPendingReset = SlicePendingReset[SliceIndex];
	SlicePendingReset[SliceIndex] = false;
	return bWasPendingReset;
}

void FTDFluidGpuBatch::AddFrameEntry(int32 ActorInputIndex, int32 SliceIndex, bool bShouldForceReset)
{
	FTDFluidGpuSliceEntry& Entry = Frame.Entries.AddDefaulted_GetRef();
	Entry.ActorInputIndex = ActorInputIndex;
	Entry.SliceIndex = SliceIndex;
	Entry.bNeedsReset = ConsumePendingReset(SliceIndex) || bShouldForceReset;
}

void FTDFluidGpuBatch::BuildFrameParameters(const FTDFluidGpuFrameInputs& FrameInputs, FTDFluidGpuPreparedFrame& InOutPreparedFrame)
{
	Frame.Entries.Sort([](const FTDFluidGpuSliceEntry& Left, const FTDFluidGpuSliceEntry& Right)
	{
		return Left.SliceIndex < Right.SliceIndex;
	});
	Frame.Parameters.Reserve(Frame.Entries.Num());

	for (int32 EntryIndex = 0; EntryIndex < Frame.Entries.Num(); ++EntryIndex)
	{
		const FTDFluidGpuSliceEntry& Entry = Frame.Entries[EntryIndex];
		const FTDFluidGpuActorInput& Actor = FrameInputs.Actors[Entry.ActorInputIndex];
		const FTDFluidGpuSimulationSettings Settings = SanitizeSettings(Actor.Settings);
		const FVector2D WindowCenter = Actor.WindowCenterPlaneCm.ContainsNaN() ? FVector2D::ZeroVector : Actor.WindowCenterPlaneCm;
		const FVector2D PlaneUpDirection = ResolvePlaneUpDirection(Actor.PlaneUpDirection);

		FTDFluidGpuStampBinWindow StampWindow;
		StampWindow.CenterPlaneCm = WindowCenter;
		StampWindow.ExtentCm = Settings.ExtentCm;
		StampWindow.Resolution = Key.ResolutionTier;

		FTDFluidGpuStampBinLimits StampLimits;
		StampLimits.MaxStampsPerTick = Settings.MaxStampsPerTick;
		const FTDFluidGpuStampBinSet ActorStampBins = FTDFluidGpuStampBins::Build(StampWindow, Actor.Stamps, StampLimits);

		const int32 FirstObstacle = Frame.Obstacles.Num();
		AppendPackedObstacles(Actor.Obstacles, WindowCenter, Frame.Obstacles);
		const int32 ObstacleCount = Frame.Obstacles.Num() - FirstObstacle;

		const ETDFluidGpuActorFlags Flags = ResolveActorFlags(Actor, Settings, PlaneUpDirection, Entry.bNeedsReset, ActorStampBins.Stamps.Num(), ObstacleCount);
		const bool bHasShift = EnumHasAnyFlags(Flags, ETDFluidGpuActorFlags::HasShift);

		FTDFluidGpuActorParameters& Parameters = Frame.Parameters.Add_GetRef(MakeActorParameters(Actor, Settings, WindowCenter, PlaneUpDirection, Key.ResolutionTier));
		Parameters.SliceIndex = Entry.SliceIndex;
		Parameters.Flags = static_cast<uint32>(Flags);
		Parameters.ShiftCells = bHasShift ? Actor.ShiftCells : FIntPoint::ZeroValue;
		Parameters.ReadbackTileOrigin = InOutPreparedFrame.ReadbackLayout.GetTileOrigin(Entry.ActorInputIndex);
		Parameters.StampBinTableOffset = FTDFluidGpuStampBins::AppendToFrame(ActorStampBins, InOutPreparedFrame.StampBins);
		Parameters.FirstObstacle = static_cast<uint32>(FirstObstacle);
		Parameters.ObstacleCount = static_cast<uint32>(ObstacleCount);

		if (Entry.bNeedsReset)
		{
			Frame.ResetEntryIndices.Add(static_cast<uint32>(EntryIndex));
		}
		if (bHasShift)
		{
			Frame.ShiftEntryIndices.Add(static_cast<uint32>(EntryIndex));
		}
		if (EnumHasAnyFlags(Flags, ETDFluidGpuActorFlags::RasterizesObstacles))
		{
			Frame.ObstacleEntryIndices.Add(static_cast<uint32>(EntryIndex));
		}
		if (EnumHasAnyFlags(Flags, ETDFluidGpuActorFlags::HasTerrainElevation))
		{
			AppendPackedTerrainUpdates(Actor.TerrainElevationUpdates, Entry.SliceIndex, Key.ResolutionTier, Frame.TerrainUpdates);
		}
	}
}
