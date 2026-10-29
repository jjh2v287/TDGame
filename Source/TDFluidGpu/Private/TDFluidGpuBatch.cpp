#include "TDFluidGpuBatch.h"

#include "TDFluidGpuNearRipple.h"
#include "TDFluidGpuStepPasses.h"
#include "TDFluidGpuFieldInput.h"

#include "HAL/IConsoleManager.h"

int32 TDFluidGpuComputeTrackBankRadiusCells(float BlurRadiusCm, double CellSizeCm)
{
	if (!(CellSizeCm > 0.0))
	{
		return 1;
	}
	return FMath::Clamp(FMath::RoundToInt(static_cast<double>(BlurRadiusCm) / CellSizeCm), 1, 16);
}

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
	constexpr int64 PainterVelocityStateBytesPerCell = 2 * FloatBytes;
	constexpr int64 MultigridRhsBytesPerCell = FloatBytes;
	constexpr int64 MultigridCorrectionBytesPerCell = FloatBytes;
	constexpr int64 MultigridApertureBytesPerCell = 2 * HalfFloatBytes;
	constexpr int64 MultigridComponentLabelBytesPerCell = 4;
	constexpr int64 HeatBytesPerCell = PingPongTextureCount * FloatBytes;
	constexpr int64 AuxOutputBytesPerCell = RgbaHalfFloatBytes;
	constexpr int32 ForcedJacobiSolverMode = 1;
	constexpr int32 ForcedMultigridSolverMode = 2;
	constexpr float BatchWaterSquareRootOfTwo = 1.41421356f;
	constexpr int32 BatchWaterPressureModeRequestMaximum = 2;
	constexpr int32 BatchWaterKernelMinimumRadius = 2;
	constexpr int32 BatchWaterKernelMaximumRadius = 7;
	constexpr float BatchWaterKernelMaximumScale = 100.0f;
	constexpr int32 BatchWaterHalfPressureDivisor = 2;
	constexpr int32 BatchWaterHalfFieldDivisor = 2;
	constexpr int32 BatchWaterQuarterFieldDivisor = 4;
	constexpr int32 BatchWaterConsoleFloat16Request = 0;
	constexpr int32 BatchWaterConsoleFloat32Request = 1;
	constexpr bool bBatchWaterVelocityAutomaticallyUsesFloat32 = true;
	constexpr int64 BatchWaterHalfPairBytesPerTexel = 2 * HalfFloatBytes;
	constexpr int64 BatchWaterFloatPairBytesPerTexel = 2 * FloatBytes;
	constexpr int64 BatchWaterFieldBytesPerCell = 4 * FloatBytes + FloatBytes + FloatBytes;
	constexpr int64 BatchWaterWetnessBytesPerCell = FloatBytes;
	constexpr int64 BatchWaterOutputBytesPerCell = 3 * RgbaHalfFloatBytes;
	constexpr int64 BatchWaterCompositeSdfBytesPerCell = HalfFloatBytes;
	constexpr int64 BatchWaterFieldDrySdfBytesPerCell = HalfFloatBytes;
	constexpr int64 BatchWaterUpsampledPressureBytesPerCell = FloatBytes;
	constexpr int64 BatchWaterPostSmoothBytesPerCell = HalfFloatBytes + FloatBytes;
	constexpr int64 BatchWaterHalfRhsMeanTileResolution = 16;
	constexpr int64 BatchWaterHalfRhsMeanPartialSumBytes = 2 * FloatBytes;
	constexpr int64 BatchWaterOpenBoundaryBytesPerCell = 2 * HalfFloatBytes;
	constexpr int64 BatchWaterKernelBytesPerHalfCell = 4 * FloatBytes + FloatBytes + FloatBytes + 2 * HalfFloatBytes + 2 * HalfFloatBytes;
	constexpr int64 BatchWaterProbeReadbackBytes = static_cast<int64>(TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame) * static_cast<int64>(sizeof(FTDFluidGpuWaterProbeSample)) * TDFluidGpuSolverConstants::ReadbackRingSize;

	TAutoConsoleVariable<int32> CVarTDFluidGpuPressureSolver(
		TEXT("TD.FluidFX.GpuPressureSolver"),
		-1,
		TEXT("GPU fluid pressure solver. Negative: follow the preset field GpuPressureSolverMode, 0: automatic (multigrid V cycles at the 512 tier and above, Jacobi below), 1: Jacobi everywhere, 2: multigrid at the 64 tier and above. Changing it moves GPU fluid actors into new batches and resets their state."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarTDFluidGpuPressureCycles(
		TEXT("TD.FluidFX.GpuPressureCycles"),
		-1,
		TEXT("Multigrid V cycles per GPU fluid pressure projection, clamped to 1..4. Negative: follow the preset field GpuMultigridCycles. Changing it resets multigrid GPU fluid actors."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuPressureMode(
		TEXT("TD.FluidFX.WaterGpuPressureMode"),
		-1,
		TEXT("GPU water pressure at the 512 tier and above. Negative: follow the preset field WaterGpuPressureMode, 0: default choice (half resolution multigrid), 1: half resolution multigrid, 2: half resolution kernel feedback on square windows. Changing it moves GPU water actors into new batches and resets their state."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuVelocityFloat32(
		TEXT("TD.FluidFX.WaterGpuVelocityFloat32"),
		-1,
		TEXT("GPU water velocity texture format. -1: automatic (RG32F at every tier), 0: RG16F, 1: RG32F. Changing it resets GPU water actors."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuRippleFloat32(
		TEXT("TD.FluidFX.WaterGpuRippleFloat32"),
		-1,
		TEXT("GPU water ripple texture format. -1: automatic (RG32F up to the 256 tier, RG16F above), 0: RG16F, 1: RG32F. Changing it resets GPU water actors."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuKernelRadius(
		TEXT("TD.FluidFX.WaterGpuKernelRadius"),
		-1,
		TEXT("Kernel feedback pressure radius in half resolution texels, clamped to 2..7. Negative: follow the preset field PressureKernelRadiusTexels."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarTDFluidWaterGpuKernelFeedback(
		TEXT("TD.FluidFX.WaterGpuKernelFeedback"),
		-1.0f,
		TEXT("Kernel feedback pressure feedback factor, clamped to 0..1. Negative: follow the preset field PressureKernelFeedback."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarTDFluidWaterGpuKernelScale(
		TEXT("TD.FluidFX.WaterGpuKernelScale"),
		-1.0f,
		TEXT("Multiplier on the kernel feedback pressure divergence scale. Negative: follow the preset field PressureKernelScale."),
		ECVF_Default);

	struct FTDFluidGpuNoiseLatticePlacement
	{
		FIntPoint Origin = FIntPoint::ZeroValue;
		FVector2f Offset = FVector2f::ZeroVector;
		FVector2f StepPerCell = FVector2f::ZeroVector;
		uint32 PeriodPacked = 0;
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

	bool IsBatchWaterLayerSettings(const FTDFluidGpuSimulationSettings& Settings)
	{
		return Settings.bUsesWaterLayer && Settings.SimMode == ETDFluidGpuSimMode::FluidSim;
	}

	bool ResolveBatchWaterFloat32Request(int32 ConsoleValue, bool bAutomaticChoice)
	{
		if (ConsoleValue == BatchWaterConsoleFloat16Request)
		{
			return false;
		}
		if (ConsoleValue == BatchWaterConsoleFloat32Request)
		{
			return true;
		}
		return bAutomaticChoice;
	}

	uint8 ClampBatchWaterPressureModeRequest(int32 Request)
	{
		return static_cast<uint8>(FMath::Clamp(Request, 0, BatchWaterPressureModeRequestMaximum));
	}

	float SanitizeBatchWaterKernelScale(float KernelScale)
	{
		return FMath::IsFinite(KernelScale) ? FMath::Clamp(KernelScale, 0.0f, BatchWaterKernelMaximumScale) : 1.0f;
	}

	int32 ResolveIntegerConsoleOverride(int32 ConsoleValue, int32 PresetValue)
	{
		return ConsoleValue >= 0 ? ConsoleValue : PresetValue;
	}

	float ResolveFloatConsoleOverride(float ConsoleValue, float PresetValue)
	{
		return ConsoleValue >= 0.0f ? ConsoleValue : PresetValue;
	}

	void ApplyWrappedAreaRules(const FTDFluidGpuSimulationSettings& Settings, FTDFluidGpuSimulationSettings& InOutSanitized)
	{
		if (Settings.AreaAddressModeRequest != TDFluidGpuSolverConstants::WrapAreaAddressModeRequest)
		{
			InOutSanitized.AreaAddressModeRequest = 0;
			return;
		}
		InOutSanitized.SimMode = ETDFluidGpuSimMode::FluidSim;
		InOutSanitized.bUsesWaterLayer = false;
		InOutSanitized.bUsesWaterWetness = false;
		InOutSanitized.bReflectsAtBoundaries = false;
		InOutSanitized.TerrainSlopeGravity = 0.0f;
		InOutSanitized.OutflowBandRatio = 0.0f;
		InOutSanitized.BorderFadeRatio = 0.0f;
		InOutSanitized.Resolution = FTDFluidGpuBatch::ResolveResolutionTier(FMath::Min(Settings.Resolution, TDFluidGpuSolverConstants::WrapAreaMaximumResolution));
		InOutSanitized.bUsesHeatChannel = Settings.bUsesHeatChannel;
	}

	FTDFluidGpuSimulationSettings SanitizeSettings(const FTDFluidGpuSimulationSettings& Settings)
	{
		FTDFluidGpuSimulationSettings Sanitized = Settings;
		Sanitized.MaxCellSpeedCmPerSec = ClampFinite(Settings.MaxCellSpeedCmPerSec, 1.0f, TNumericLimits<float>::Max());
		Sanitized.bUsesWaterLayer = IsBatchWaterLayerSettings(Settings);
		Sanitized.Resolution = Sanitized.bUsesWaterLayer
			? FTDFluidGpuBatch::ResolveWaterResolutionTier(Settings.Resolution)
			: FTDFluidGpuBatch::ResolveResolutionTier(Settings.Resolution);
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
		Sanitized.MaxStampsPerBin = FMath::Clamp(Settings.MaxStampsPerBin, 1, TDFluidGpuSolverConstants::MaximumStampsPerBin);
		Sanitized.MassSplatRadiusCapCells = FMath::Clamp(Settings.MassSplatRadiusCapCells, 1, TDFluidGpuSolverConstants::MaximumMassSplatRadiusCapCells);
		Sanitized.AmbientDensity = ClampFinite(Settings.AmbientDensity, 0.0f, 1.0f);
		Sanitized.AmbientRefillPerSec = ClampFinite(Settings.AmbientRefillPerSec, 0.0f, 100.0f);
		Sanitized.FlowNoiseAccelerationCmPerSec2 = ClampFinite(Settings.FlowNoiseAccelerationCmPerSec2, 0.0f, 10000.0f);
		Sanitized.FlowNoiseScaleCm = ClampFinite(Settings.FlowNoiseScaleCm, 1.0f, 100000.0f);
		Sanitized.FlowNoiseSpeed = ClampFinite(Settings.FlowNoiseSpeed, 0.0f, 100.0f);
		Sanitized.AmbientNoiseAmount = ClampFinite(Settings.AmbientNoiseAmount, 0.0f, 1.0f);
		Sanitized.BuoyancyCmPerSec2 = ClampFinite(Settings.BuoyancyCmPerSec2, -20000.0f, 20000.0f);
		Sanitized.OutflowBandRatio = ClampFinite(Settings.OutflowBandRatio, 0.0f, 0.45f);
		Sanitized.OutflowRatePerSec = ClampFinite(Settings.OutflowRatePerSec, 0.0f, 50.0f);
		Sanitized.HeatCoolingPerSec = ClampFinite(Settings.HeatCoolingPerSec, 0.0f, 50.0f);
		Sanitized.HeatBuoyancyCmPerSec2 = ClampFinite(Settings.HeatBuoyancyCmPerSec2, 0.0f, 20000.0f);
		Sanitized.EdgeBounciness = ClampFinite(Settings.EdgeBounciness, 0.0f, 1.0f);
		Sanitized.BorderFadeExponent = FMath::IsFinite(Settings.BorderFadeExponent) ? FMath::Clamp(Settings.BorderFadeExponent, 0.25f, 8.0f) : 1.0f;
		Sanitized.VortexTangentialAccelerationCmPerSec2 = FMath::IsFinite(Settings.VortexTangentialAccelerationCmPerSec2) ? FMath::Clamp(Settings.VortexTangentialAccelerationCmPerSec2, -20000.0f, 20000.0f) : 0.0f;
		Sanitized.VortexRadialDriftCmPerSec = FMath::IsFinite(Settings.VortexRadialDriftCmPerSec) ? FMath::Clamp(Settings.VortexRadialDriftCmPerSec, -Sanitized.MaxCellSpeedCmPerSec, Sanitized.MaxCellSpeedCmPerSec) : 0.0f;
		Sanitized.BrushVelocityResponsePerSec = ClampFinite(Settings.BrushVelocityResponsePerSec, 0.0f, TNumericLimits<float>::Max());
		Sanitized.BrushDisplacementRatio = ClampFinite(Settings.BrushDisplacementRatio, 0.0f, TNumericLimits<float>::Max());
		Sanitized.BrushEraseRatePerSec = ClampFinite(Settings.BrushEraseRatePerSec, 0.0f, TNumericLimits<float>::Max());
		Sanitized.TrackBankStrength = ClampFinite(Settings.TrackBankStrength, 0.0f, 2.0f);
		Sanitized.TrackBankBlurRadiusCm = ClampFinite(Settings.TrackBankBlurRadiusCm, 2.0f, 60.0f);
		Sanitized.TrackBankHeightLimit = ClampFinite(Settings.TrackBankHeightLimit, 0.0f, 1.0f);
		Sanitized.VortexRadiusRatio = ClampFinite(Settings.VortexRadiusRatio, 0.01f, 0.5f);
		Sanitized.bUsesHeatChannel = Settings.bUsesHeatChannel && Settings.SimMode == ETDFluidGpuSimMode::FluidSim && !Sanitized.bUsesWaterLayer;
		Sanitized.WaterDepthReferenceCm = ClampFinite(Settings.WaterDepthReferenceCm, 1.0f, 100000.0f);
		Sanitized.FlowGuideResponsePerSec = ClampFinite(Settings.FlowGuideResponsePerSec, 0.0f, 100.0f);
		Sanitized.RapidsWhitewaterPerSec = ClampFinite(Settings.RapidsWhitewaterPerSec, 0.0f, 1000.0f);
		Sanitized.WhitewaterFromVorticity = ClampFinite(Settings.WhitewaterFromVorticity, 0.0f, 100.0f);
		Sanitized.WhitewaterVorticityThresholdPerSec = ClampFinite(Settings.WhitewaterVorticityThresholdPerSec, 0.0f, 1000.0f);
		Sanitized.RippleWaveSpeedCmPerSec = ClampFinite(Settings.RippleWaveSpeedCmPerSec, 0.0f, 2000.0f);
		Sanitized.RippleDampingPerSec = ClampFinite(Settings.RippleDampingPerSec, 0.0f, 100.0f);
		Sanitized.RippleHeightReferenceCm = ClampFinite(Settings.RippleHeightReferenceCm, 0.01f, 1000.0f);
		Sanitized.RippleFlowAdvection = ClampFinite(Settings.RippleFlowAdvection, 0.0f, 4.0f);
		Sanitized.WaterPressureModeRequest = ClampBatchWaterPressureModeRequest(Settings.WaterPressureModeRequest);
		Sanitized.WaterKernelRadius = FMath::Clamp(Settings.WaterKernelRadius, BatchWaterKernelMinimumRadius, BatchWaterKernelMaximumRadius);
		Sanitized.WaterKernelFeedback = ClampFinite(Settings.WaterKernelFeedback, 0.0f, 1.0f);
		Sanitized.WaterKernelScale = SanitizeBatchWaterKernelScale(Settings.WaterKernelScale);
		Sanitized.WhitewaterDissipation = ClampFinite(Settings.WhitewaterDissipation, 0.0f, 10.0f);
		Sanitized.BankWetHeightCm = ClampFinite(Settings.BankWetHeightCm, 0.0f, 1000.0f);
		Sanitized.WetnessFromWhitewater = ClampFinite(Settings.WetnessFromWhitewater, 0.0f, 1.0f);
		Sanitized.SwashHeightCm = ClampFinite(Settings.SwashHeightCm, 0.0f, 1000.0f);
		Sanitized.CoastWavePeriodSeconds = ClampFinite(Settings.CoastWavePeriodSeconds, 0.1f, 1000.0f);
		Sanitized.WetnessDryingSeconds = ClampFinite(Settings.WetnessDryingSeconds, 0.01f, 10000.0f);
		Sanitized.DensityAccumulationPerSec = ClampFinite(Settings.DensityAccumulationPerSec, 0.0f, 20.0f);
		Sanitized.DenseDensityCeiling = ClampFinite(Settings.DenseDensityCeiling, 1.0f, 8.0f);
		Sanitized.DenseGrowthThreshold = ClampFinite(Settings.DenseGrowthThreshold, 0.0f, 1.0f);
		Sanitized.FlowNoiseSpeedMaskReferenceCmPerSec = ClampFinite(Settings.FlowNoiseSpeedMaskReferenceCmPerSec, 0.0f, 1200.0f);
		ApplyWrappedAreaRules(Settings, Sanitized);
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

	int32 ResolvePeriodicLatticeCount(double ExtentCm, float SpacingCm)
	{
		return FMath::Max(1, FMath::RoundToInt(static_cast<float>(ExtentCm) / SpacingCm));
	}

	FTDFluidGpuNoiseLatticePlacement MakePeriodicNoiseLatticePlacement(const FTDFluidGpuSimulationSettings& Settings, int32 Resolution, float SpacingCm)
	{
		const int32 PeriodX = ResolvePeriodicLatticeCount(Settings.ExtentCm.X, SpacingCm);
		const int32 PeriodY = ResolvePeriodicLatticeCount(Settings.ExtentCm.Y, SpacingCm);
		FTDFluidGpuNoiseLatticePlacement Placement;
		Placement.StepPerCell = FVector2f(
			static_cast<float>(static_cast<double>(PeriodX) / Resolution),
			static_cast<float>(static_cast<double>(PeriodY) / Resolution));
		Placement.PeriodPacked = static_cast<uint32>(PeriodX) | (static_cast<uint32>(PeriodY) << 16);
		return Placement;
	}

	FTDFluidGpuNoiseLatticePlacement MakeNoiseLatticePlacement(const FTDFluidGpuSimulationSettings& Settings, const FVector2D& WindowCenter, int32 Resolution, float ScaleMultiplier)
	{
		const float LargestCellSizeCm = static_cast<float>(FMath::Max(Settings.ExtentCm.X, Settings.ExtentCm.Y)) / static_cast<float>(Resolution);
		const float SpacingCm = FMath::Max(Settings.FlowNoiseScaleCm, LargestCellSizeCm) * ScaleMultiplier;
		if (Settings.AreaAddressModeRequest == TDFluidGpuSolverConstants::WrapAreaAddressModeRequest)
		{
			return MakePeriodicNoiseLatticePlacement(Settings, Resolution, SpacingCm);
		}
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
		Parameters.StampVelocityBlend = FMath::Min(1.0f, Settings.BrushVelocityResponsePerSec * StampDeltaTime);
		Parameters.VorticityStrength = Settings.VorticityStrength;
		Parameters.AdvectionStepScale = FVector2f((StepDeltaTime * Settings.AdvectionSpeedScale) / CellSizeX, (StepDeltaTime * Settings.AdvectionSpeedScale) / CellSizeY);
		Parameters.AdvectVelocityDampingFactor = 1.0f / (1.0f + Settings.VelocityDamping * StepDeltaTime * AdvectVelocityDampingScale);
		Parameters.DensityDissipationFactor = 1.0f / (1.0f + Settings.DensityDissipation * StepDeltaTime);
		Parameters.WhitewaterDissipationFactor = Settings.bUsesWhitewaterDissipation ? 1.0f / (1.0f + Settings.WhitewaterDissipation * StepDeltaTime) : Parameters.DensityDissipationFactor;
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
		Parameters.FlowNoiseLatticePeriodPacked = FlowNoiseLattice.PeriodPacked;
		Parameters.AmbientNoiseLatticePeriodPacked = AmbientNoiseLattice.PeriodPacked;
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
		Parameters.MaxCellSpeedCmPerSec = Settings.MaxCellSpeedCmPerSec;
		Parameters.BrushDisplacementRatio = Settings.BrushDisplacementRatio;
		Parameters.TrackBankStrength = Settings.TrackBankStrength;
		Parameters.TrackBankHeightLimit = Settings.TrackBankHeightLimit;
		Parameters.TrackBankRadiusCells = static_cast<uint32>(TDFluidGpuComputeTrackBankRadiusCells(Settings.TrackBankBlurRadiusCm, Settings.ExtentCm.X / static_cast<double>(FMath::Max(Resolution, 1))));
		Parameters.DenseDensityCeiling = FMath::Max(Settings.DenseDensityCeiling, 1.0f);
		Parameters.DenseGrowthFactor = FMath::Exp(Settings.DensityAccumulationPerSec * StepDeltaTime);
		Parameters.DenseGrowthThreshold = Settings.DenseGrowthThreshold;
		Parameters.FlowNoiseSpeedMaskInverseCmPerSec = Settings.FlowNoiseSpeedMaskReferenceCmPerSec > 0.0f ? 1.0f / Settings.FlowNoiseSpeedMaskReferenceCmPerSec : 0.0f;
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
		const bool bHasObstacles = ObstacleCount > 0 || TDFluidGpuFieldInput::HasCollisionField(Actor);
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
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesBuoyancy, bRunsFluidForces && bHasPlaneUp && Settings.BuoyancyCmPerSec2 != 0.0f);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesVorticity, bRunsFluidForces && Settings.VorticityStrength > 0.0f);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesOutflow, bRunsFluidForces && bHasPlaneUp && Settings.OutflowRatePerSec > 0.0f && Settings.OutflowBandRatio > 0.0f);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesAmbientRefill, bUsesAmbientRefill);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesAmbientNoise, bUsesAmbientRefill && Settings.AmbientNoiseAmount > 0.0f);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::HasStamps, StampCount > 0);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesHeatChannel, bRunsFluidForces && Settings.bUsesHeatChannel);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesBoundaryReflection, bRunsFluidForces && Settings.bReflectsAtBoundaries);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesVortexForce, bRunsFluidForces && Settings.VortexRadiusRatio > 0.0f && (Settings.VortexTangentialAccelerationCmPerSec2 != 0.0f || Settings.VortexRadialDriftCmPerSec != 0.0f));
		AddFlagIf(Flags, ETDFluidGpuActorFlags::WritesAuxOutput, bRunsFluidForces && Settings.bUsesHeatChannel && Actor.AuxOutput != nullptr);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesTrackBank, bIsSimplePainter && Settings.TrackBankStrength > 0.0f);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::WritesPainterVelocity, bIsSimplePainter && Settings.bWritesPainterVelocity);
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesDenseDensity, bRunsFluidForces && !Settings.bUsesWaterLayer && (Settings.DenseDensityCeiling > 1.0f || Settings.DensityAccumulationPerSec > 0.0f));
		AddFlagIf(Flags, ETDFluidGpuActorFlags::UsesFlowNoiseSpeedMask, bRunsFluidForces && Settings.FlowNoiseAccelerationCmPerSec2 > 0.0f && Settings.FlowNoiseSpeedMaskReferenceCmPerSec > 0.0f && !Settings.bUsesWaterLayer);
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

	void ApplyBatchWaterPressureKey(const FTDFluidGpuSimulationSettings& Settings, FTDFluidGpuBatchKey& InOutKey)
	{
		if (InOutKey.WaterPressureMode == ETDFluidGpuWaterPressureMode::JacobiOpenBorders)
		{
			InOutKey.PressureSolver = ETDFluidGpuPressureSolver::Jacobi;
			InOutKey.PressureIterations = FMath::Clamp(Settings.PressureIterations, TDFluidGpuSolverConstants::MinimumPressureIterations, TDFluidGpuSolverConstants::MaximumPressureIterations);
			return;
		}

		if (InOutKey.WaterPressureMode == ETDFluidGpuWaterPressureMode::HalfResolutionMultigrid)
		{
			InOutKey.PressureSolver = ETDFluidGpuPressureSolver::Multigrid;
			InOutKey.MultigridCycles = FMath::Clamp(Settings.WaterMultigridCycles, TDFluidGpuSolverConstants::MinimumMultigridCycles, TDFluidGpuSolverConstants::MaximumMultigridCycles);
			return;
		}

		InOutKey.PressureSolver = ETDFluidGpuPressureSolver::Jacobi;
		InOutKey.PressureIterations = 0;
		InOutKey.MultigridCycles = 0;
	}

	int64 ComputeBatchWaterHalfPressureExtraBytes(int32 ResolutionTier)
	{
		const int64 Resolution = ResolutionTier;
		const int64 HalfResolution = Resolution / BatchWaterHalfPressureDivisor;
		const int64 TilesPerRow = FMath::DivideAndRoundUp<int64>(HalfResolution, BatchWaterHalfRhsMeanTileResolution);
		const int64 HalfRhsMeanBytes = TilesPerRow * TilesPerRow * BatchWaterHalfRhsMeanPartialSumBytes + FloatBytes;
		const int64 PostSmoothBytes = ComputeTDFluidGpuWaterHalfPostSmoothIterations() > 0 ? Resolution * Resolution * BatchWaterPostSmoothBytesPerCell : 0;
		return HalfRhsMeanBytes + PostSmoothBytes;
	}

	int64 ComputeBatchWaterPressureTransientBytes(ETDFluidGpuWaterPressureMode PressureMode, int32 ResolutionTier)
	{
		const int64 Resolution = ResolutionTier;
		const int64 CellCount = Resolution * Resolution;
		if (PressureMode == ETDFluidGpuWaterPressureMode::JacobiOpenBorders)
		{
			return CellCount * DivergenceCurlBytesPerCell;
		}

		const int64 UpsampledPressureBytes = CellCount * BatchWaterUpsampledPressureBytesPerCell + ComputeBatchWaterHalfPressureExtraBytes(ResolutionTier);
		if (PressureMode == ETDFluidGpuWaterPressureMode::HalfResolutionMultigrid)
		{
			return UpsampledPressureBytes
				+ FTDFluidGpuBatch::ComputeMultigridTransientBytes(ResolutionTier)
				+ FTDFluidGpuBatch::ComputeWaterMultigridOpenBoundaryBytes(ResolutionTier);
		}

		const int64 HalfResolution = Resolution / BatchWaterHalfPressureDivisor;
		return UpsampledPressureBytes + HalfResolution * HalfResolution * BatchWaterKernelBytesPerHalfCell;
	}

	void AddBatchWaterFlagIf(ETDFluidGpuWaterFlags& InOutFlags, ETDFluidGpuWaterFlags Flag, bool bCondition)
	{
		if (!bCondition)
		{
			return;
		}

		InOutFlags |= Flag;
	}

	ETDFluidGpuWaterFlags ResolveBatchWaterFlags(const FTDFluidGpuSimulationSettings& Settings, bool bNeedsReset, bool bReachedRippleSubstepCap, bool bUpdatesWetness, bool bResetsWetness)
	{
		const bool bUsesRipples = Settings.RippleWaveSpeedCmPerSec > 0.0f;

		ETDFluidGpuWaterFlags Flags = ETDFluidGpuWaterFlags::None;
		AddBatchWaterFlagIf(Flags, ETDFluidGpuWaterFlags::UsesOpenBorders, Settings.bOpenWaterBorders);
		AddBatchWaterFlagIf(Flags, ETDFluidGpuWaterFlags::TreatsDryCellsAsSolid, Settings.bTreatDryCellsAsSolid);
		AddBatchWaterFlagIf(Flags, ETDFluidGpuWaterFlags::InitializesAllVelocities, bNeedsReset);
		AddBatchWaterFlagIf(Flags, ETDFluidGpuWaterFlags::UsesRipples, bUsesRipples);
		AddBatchWaterFlagIf(Flags, ETDFluidGpuWaterFlags::UsesRippleAdvection, bUsesRipples && Settings.RippleFlowAdvection > 0.0f);
		AddBatchWaterFlagIf(Flags, ETDFluidGpuWaterFlags::UsesFlowGuides, Settings.FlowGuideResponsePerSec > 0.0f);
		AddBatchWaterFlagIf(Flags, ETDFluidGpuWaterFlags::UsesWhitewaterFromVorticity, Settings.WhitewaterFromVorticity > 0.0f);
		AddBatchWaterFlagIf(Flags, ETDFluidGpuWaterFlags::UsesRapidsWhitewater, Settings.RapidsWhitewaterPerSec > 0.0f);
		AddBatchWaterFlagIf(Flags, ETDFluidGpuWaterFlags::UsesTerrainSlopeDryOnly, Settings.TerrainSlopeGravity > 0.0f);
		AddBatchWaterFlagIf(Flags, ETDFluidGpuWaterFlags::ReachedRippleSubstepCap, bReachedRippleSubstepCap);
		AddBatchWaterFlagIf(Flags, ETDFluidGpuWaterFlags::UpdatesWetness, bUpdatesWetness);
		AddBatchWaterFlagIf(Flags, ETDFluidGpuWaterFlags::ResetsWetness, bResetsWetness);
		return Flags;
	}

	FTDFluidGpuWaterParameters MakeWaterParameters(const FTDFluidGpuBatchKey& Key, const FTDFluidGpuActorInput& Actor, const FTDFluidGpuSimulationSettings& Settings, bool bNeedsReset)
	{
		const int32 ResolutionTier = Key.ResolutionTier;
		const float ResolutionAsFloat = static_cast<float>(ResolutionTier);
		const float CellSizeX = static_cast<float>(Settings.ExtentCm.X / ResolutionAsFloat);
		const float CellSizeY = static_cast<float>(Settings.ExtentCm.Y / ResolutionAsFloat);
		const float StepDeltaTime = FMath::Min(Actor.DeltaTimeSeconds, TDFluidGpuSolverConstants::MaximumStepDeltaTimeSeconds);
		const bool bUsesJacobiPressure = Key.WaterPressureMode == ETDFluidGpuWaterPressureMode::JacobiOpenBorders;
		const bool bUsesKernelPressure = Key.WaterPressureMode == ETDFluidGpuWaterPressureMode::HalfResolutionKernelFeedback;
		const int32 FieldDivisor = FTDFluidGpuBatch::ComputeWaterFieldDivisor(ResolutionTier);
		const int32 PressureDivisor = bUsesJacobiPressure ? 1 : BatchWaterHalfPressureDivisor;
		const float RippleWaveSpeed = Settings.RippleWaveSpeedCmPerSec;

		bool bReachedRippleSubstepCap = false;
		float RippleWaveSpeedScale = 1.0f;
		const int32 RippleSubstepCount = FTDFluidGpuBatch::ComputeWaterRippleSubstepCount(RippleWaveSpeed, StepDeltaTime, FMath::Min(CellSizeX, CellSizeY), ResolutionTier, bReachedRippleSubstepCap, RippleWaveSpeedScale);
		const float ScaledRippleWaveSpeed = RippleWaveSpeed * RippleWaveSpeedScale;
		const bool bUpdatesWetness = Actor.bUpdatesWaterWetness;
		const bool bResetsWetness = bUpdatesWetness && (bNeedsReset || Actor.bResetsWaterWetness);

		FTDFluidGpuWaterParameters WaterParameters;
		WaterParameters.WaterFlags = static_cast<uint32>(ResolveBatchWaterFlags(Settings, bNeedsReset, bReachedRippleSubstepCap, bUpdatesWetness, bResetsWetness));
		WaterParameters.FieldResolution = ResolutionTier / FieldDivisor;
		WaterParameters.FieldDivisor = FieldDivisor;
		WaterParameters.PressureResolution = ResolutionTier / PressureDivisor;
		WaterParameters.FieldOrigin = Actor.WaterFieldOrigin;
		WaterParameters.PressureDivisor = PressureDivisor;
		WaterParameters.RippleSubstepCount = static_cast<uint32>(RippleSubstepCount);
		WaterParameters.FlowGuideResponseBlend = 1.0f - FMath::Exp(-Settings.FlowGuideResponsePerSec * StepDeltaTime);
		WaterParameters.RippleSubstepSeconds = RippleSubstepCount > 0 ? StepDeltaTime / static_cast<float>(RippleSubstepCount) : 0.0f;
		WaterParameters.RippleWaveSpeedSquared = ScaledRippleWaveSpeed * ScaledRippleWaveSpeed;
		WaterParameters.RippleDampingPerSec = Settings.RippleDampingPerSec;
		WaterParameters.InvCellSizeSquaredCm = FVector2f(1.0f / (CellSizeX * CellSizeX), 1.0f / (CellSizeY * CellSizeY));
		WaterParameters.RippleAdvectionStepScale = FVector2f((StepDeltaTime * Settings.RippleFlowAdvection) / CellSizeX, (StepDeltaTime * Settings.RippleFlowAdvection) / CellSizeY);
		WaterParameters.WhitewaterVorticityStep = Settings.WhitewaterFromVorticity * StepDeltaTime;
		WaterParameters.WhitewaterVorticityThresholdPerSec = Settings.WhitewaterVorticityThresholdPerSec;
		WaterParameters.RapidsWhitewaterStep = Settings.RapidsWhitewaterPerSec * StepDeltaTime;
		WaterParameters.WaterDepthReferenceCm = Settings.WaterDepthReferenceCm;
		WaterParameters.RippleHeightReferenceCm = Settings.RippleHeightReferenceCm;
		WaterParameters.TerrainReferenceZCm = FMath::IsFinite(Actor.WaterTerrainReferenceZCm) ? Actor.WaterTerrainReferenceZCm : 0.0f;
		WaterParameters.WetnessRetainFactor = Actor.WaterWetnessRetainFactor;
		WaterParameters.SwashReachCm = Actor.WaterSwashReachCm;
		WaterParameters.BankWetHeightCm = Settings.BankWetHeightCm;
		WaterParameters.InvBankWetHeightCm = Settings.BankWetHeightCm > 0.0f ? 1.0f / Settings.BankWetHeightCm : 0.0f;
		WaterParameters.WetnessFromWhitewater = Settings.WetnessFromWhitewater;
		if (!bUsesKernelPressure)
		{
			return WaterParameters;
		}

		const float KernelRadius = static_cast<float>(Settings.WaterKernelRadius);
		WaterParameters.KernelRadius = static_cast<uint32>(Settings.WaterKernelRadius);
		WaterParameters.KernelFeedback = Settings.WaterKernelFeedback;
		WaterParameters.KernelDivergenceScale = 0.5f * KernelRadius * KernelRadius * Settings.WaterKernelScale;
		return WaterParameters;
	}

	bool IsBatchWaterFieldCellInside(const FIntPoint& FieldCell, int32 FieldResolution)
	{
		return FieldCell.X >= 0 && FieldCell.X < FieldResolution && FieldCell.Y >= 0 && FieldCell.Y < FieldResolution;
	}

	bool IsFiniteBatchWaterGuideUpdate(const FTDFluidGpuWaterGuideCellUpdate& Update)
	{
		return FMath::IsFinite(Update.SurfaceHeightCm)
			&& !Update.TargetVelocityCmPerSec.ContainsNaN()
			&& FMath::IsFinite(Update.GuideWeight)
			&& FMath::IsFinite(Update.GuideDescent);
	}

	void AppendBatchWaterGuideUpdates(TConstArrayView<FTDFluidGpuWaterGuideCellUpdate> Updates, int32 EntryIndex, int32 FieldResolution, TArray<FTDFluidGpuPackedWaterGuideUpdate>& InOutUpdates)
	{
		TMap<FIntPoint, int32> UpdateIndexByFieldCell;
		UpdateIndexByFieldCell.Reserve(Updates.Num());
		for (const FTDFluidGpuWaterGuideCellUpdate& Update : Updates)
		{
			if (!IsBatchWaterFieldCellInside(Update.FieldCell, FieldResolution) || !IsFiniteBatchWaterGuideUpdate(Update))
			{
				continue;
			}

			FTDFluidGpuPackedWaterGuideUpdate Packed;
			Packed.EntryIndex = static_cast<uint32>(EntryIndex);
			Packed.FieldCellX = static_cast<uint32>(Update.FieldCell.X);
			Packed.FieldCellY = static_cast<uint32>(Update.FieldCell.Y);
			Packed.SurfaceHeightCm = Update.SurfaceHeightCm;
			Packed.GuideValues = FVector4f(Update.TargetVelocityCmPerSec.X, Update.TargetVelocityCmPerSec.Y, Update.GuideWeight, Update.GuideDescent);
			if (const int32* ExistingIndex = UpdateIndexByFieldCell.Find(Update.FieldCell))
			{
				InOutUpdates[*ExistingIndex] = Packed;
				continue;
			}

			UpdateIndexByFieldCell.Add(Update.FieldCell, InOutUpdates.Add(Packed));
		}
	}

	void AppendBatchWaterTerrainUpdates(TConstArrayView<FTDFluidGpuTerrainCellUpdate> Updates, int32 EntryIndex, int32 FieldResolution, TArray<FTDFluidGpuPackedWaterTerrainUpdate>& InOutUpdates)
	{
		TMap<FIntPoint, int32> UpdateIndexByFieldCell;
		UpdateIndexByFieldCell.Reserve(Updates.Num());
		for (const FTDFluidGpuTerrainCellUpdate& Update : Updates)
		{
			if (!IsBatchWaterFieldCellInside(Update.Cell, FieldResolution) || !FMath::IsFinite(Update.ElevationCm))
			{
				continue;
			}

			FTDFluidGpuPackedWaterTerrainUpdate Packed;
			Packed.EntryIndex = static_cast<uint32>(EntryIndex);
			Packed.FieldCellX = static_cast<uint32>(Update.Cell.X);
			Packed.FieldCellY = static_cast<uint32>(Update.Cell.Y);
			Packed.ElevationCm = Update.ElevationCm;
			if (const int32* ExistingIndex = UpdateIndexByFieldCell.Find(Update.Cell))
			{
				InOutUpdates[*ExistingIndex] = Packed;
				continue;
			}

			UpdateIndexByFieldCell.Add(Update.Cell, InOutUpdates.Add(Packed));
		}
	}

	void AppendBatchWaterProbeRequests(const FTDFluidGpuActorInput& Actor, int32 EntryIndex, int32 Resolution, TArray<FTDFluidGpuWaterProbeRequest>& InOutRequests)
	{
		for (int32 ProbeIndex = 0; ProbeIndex < Actor.WaterProbeCells.Num(); ++ProbeIndex)
		{
			const int32 ProbeSlot = Actor.WaterProbeFirstSlot + ProbeIndex;
			if (ProbeSlot < 0 || ProbeSlot >= TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame)
			{
				continue;
			}

			const FIntPoint& ProbeCell = Actor.WaterProbeCells[ProbeIndex];
			const bool bIsInsideWindow = ProbeCell.X >= 0 && ProbeCell.X < Resolution && ProbeCell.Y >= 0 && ProbeCell.Y < Resolution;

			FTDFluidGpuWaterProbeRequest& Request = InOutRequests.AddDefaulted_GetRef();
			Request.EntryIndex = static_cast<uint32>(EntryIndex);
			Request.ProbeSlot = static_cast<uint32>(ProbeSlot);
			Request.CellX = bIsInsideWindow ? ProbeCell.X : -1;
			Request.CellY = bIsInsideWindow ? ProbeCell.Y : -1;
		}
	}

	void AppendBatchWaterEntry(const FTDFluidGpuBatchKey& Key, int32 EntryIndex, const FTDFluidGpuSliceEntry& Entry, const FTDFluidGpuActorInput& Actor, const FTDFluidGpuSimulationSettings& Settings, FTDFluidGpuBatchFrame& InOutFrame)
	{
		const FTDFluidGpuWaterParameters& WaterParameters = InOutFrame.WaterParameters.Add_GetRef(MakeWaterParameters(Key, Actor, Settings, Entry.bNeedsReset));
		InOutFrame.WaterRippleSubstepMaximum = FMath::Max(InOutFrame.WaterRippleSubstepMaximum, static_cast<int32>(WaterParameters.RippleSubstepCount));
		if (EnumHasAnyFlags(static_cast<ETDFluidGpuWaterFlags>(WaterParameters.WaterFlags), ETDFluidGpuWaterFlags::ReachedRippleSubstepCap))
		{
			++InOutFrame.WaterRippleSubstepCapHitCount;
		}

		AppendBatchWaterGuideUpdates(Actor.WaterGuideUpdates, EntryIndex, WaterParameters.FieldResolution, InOutFrame.WaterGuideUpdates);
		AppendBatchWaterTerrainUpdates(Actor.WaterTerrainUpdates, EntryIndex, WaterParameters.FieldResolution, InOutFrame.WaterTerrainUpdates);
		AppendBatchWaterProbeRequests(Actor, EntryIndex, Key.ResolutionTier, InOutFrame.WaterProbeRequests);
	}

	int32 ComputeBatchWaterProbeSlotCount(const FTDFluidGpuFrameInputs& FrameInputs)
	{
		int32 ProbeSlotCount = 0;
		for (const FTDFluidGpuActorInput& Actor : FrameInputs.Actors)
		{
			ProbeSlotCount = FMath::Max(ProbeSlotCount, Actor.WaterProbeFirstSlot + Actor.WaterProbeCells.Num());
		}
		return FMath::Min(ProbeSlotCount, TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame);
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
	BatchKey.bWrapsArea = Settings.AreaAddressModeRequest == TDFluidGpuSolverConstants::WrapAreaAddressModeRequest && !Settings.bUsesWaterLayer;
	if (BatchKey.bWrapsArea)
	{
		BatchKey.ResolutionTier = ResolveResolutionTier(FMath::Min(Settings.Resolution, TDFluidGpuSolverConstants::WrapAreaMaximumResolution));
	}
	BatchKey.bWritesPainterVelocity = Settings.SimMode == ETDFluidGpuSimMode::SimplePainter && Settings.bWritesPainterVelocity;
	if (Settings.SimMode == ETDFluidGpuSimMode::SimplePainter)
	{
		return BatchKey;
	}

	if (Settings.bUsesWaterLayer)
	{
		BatchKey.bUsesWaterLayer = true;
		BatchKey.ResolutionTier = ResolveWaterResolutionTier(Settings.Resolution);
		BatchKey.WaterPressureMode = ResolveWaterPressureMode(Settings);
		BatchKey.bUsesFloat32WaterVelocity = Settings.bUsesFloat32WaterVelocity;
		BatchKey.bUsesFloat32Ripple = Settings.bUsesFloat32Ripple;
		ApplyBatchWaterPressureKey(Settings, BatchKey);
		return BatchKey;
	}

	BatchKey.bUsesHeatChannel = Settings.bUsesHeatChannel;
	BatchKey.MassInputDivisor = Settings.bAcceptsMassInput ? TDFluidGpuMassInput::ResolveDivisor(Settings.MassInputDivisor) : 0;
	if (BatchKey.bWrapsArea)
	{
		BatchKey.PressureSolver = ETDFluidGpuPressureSolver::Jacobi;
		BatchKey.PressureIterations = FMath::Clamp(Settings.PressureIterations, TDFluidGpuSolverConstants::MinimumPressureIterations, TDFluidGpuSolverConstants::MaximumPressureIterations);
		return BatchKey;
	}
	BatchKey.PressureSolver = ResolvePressureSolver(BatchKey.ResolutionTier, Settings.PressureSolverModeRequest);
	if (BatchKey.PressureSolver == ETDFluidGpuPressureSolver::Multigrid)
	{
		BatchKey.MultigridCycles = FMath::Clamp(ResolveIntegerConsoleOverride(CVarTDFluidGpuPressureCycles.GetValueOnAnyThread(), Settings.MultigridCyclesRequest), TDFluidGpuSolverConstants::MinimumMultigridCycles, TDFluidGpuSolverConstants::MaximumMultigridCycles);
		return BatchKey;
	}

	BatchKey.PressureIterations = FMath::Clamp(Settings.PressureIterations, TDFluidGpuSolverConstants::MinimumPressureIterations, TDFluidGpuSolverConstants::MaximumPressureIterations);
	return BatchKey;
}

ETDFluidGpuPressureSolver FTDFluidGpuBatch::ResolvePressureSolver(int32 ResolutionTier, uint8 PressureSolverModeRequest)
{
	const int32 SolverMode = ResolveIntegerConsoleOverride(CVarTDFluidGpuPressureSolver.GetValueOnAnyThread(), PressureSolverModeRequest);
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

FTDFluidGpuMemoryEstimate FTDFluidGpuBatch::EstimateActorMemory(ETDFluidGpuSimMode SimMode, int32 RequestedResolution, bool bUsesHeatChannel, bool bUsesTransmittance, uint8 PressureSolverModeRequest, int32 MassInputDivisor, bool bWritesPainterVelocity)
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
		if (bWritesPainterVelocity)
		{
			Estimate.StateBytes += CellCount * PainterVelocityStateBytesPerCell;
			Estimate.OutputBytes = CellCount * (PainterOutputBytesPerCell + RgbaHalfFloatBytes);
		}
		return Estimate;
	}

	Estimate.StateBytes = CellCount * (VelocityBytesPerCell + DensityBytesPerCell + SurfaceDisplacementBytesPerCell + PressureSolutionBytesPerCell + ObstacleSdfBytesPerCell + TerrainElevationBytesPerCell);
	Estimate.TransientBytes = ResolvePressureSolver(static_cast<int32>(Resolution), PressureSolverModeRequest) == ETDFluidGpuPressureSolver::Multigrid
		? ComputeMultigridTransientBytes(static_cast<int32>(Resolution))
		: CellCount * DivergenceCurlBytesPerCell;
	Estimate.OutputBytes = CellCount * FluidOutputBytesPerCell;
	if (bUsesHeatChannel)
	{
		Estimate.StateBytes += CellCount * HeatBytesPerCell;
		Estimate.OutputBytes += CellCount * AuxOutputBytesPerCell;
	}
	if (MassInputDivisor > 0)
	{
		Estimate.StateBytes += TDFluidGpuMassInput::ComputeSliceBytes(static_cast<int32>(Resolution), MassInputDivisor);
	}
	if (bUsesTransmittance)
	{
		const int64 TransmittanceResolution = ComputeTransmittanceResolution(static_cast<int32>(Resolution));
		Estimate.OutputBytes += TransmittanceResolution * TransmittanceResolution * RgbaHalfFloatBytes;
	}
	return Estimate;
}

int32 FTDFluidGpuBatch::ResolveWaterResolutionTier(int32 RequestedResolution)
{
	const int32 ClampedResolution = FMath::Clamp(RequestedResolution, TDFluidGpuSolverConstants::MinimumResolution, TDFluidGpuSolverConstants::MaximumResolution);
	const bool bUsesExactTier = ClampedResolution > TDFluidGpuSolverConstants::WaterPowerOfTwoTierMaximumSmallResolution
		&& ClampedResolution <= TDFluidGpuSolverConstants::WaterExactTierMaximumResolution;
	if (!bUsesExactTier)
	{
		return ResolveResolutionTier(ClampedResolution);
	}

	return FMath::DivideAndRoundUp(ClampedResolution, TDFluidGpuSolverConstants::WaterExactTierGranularity) * TDFluidGpuSolverConstants::WaterExactTierGranularity;
}

int32 FTDFluidGpuBatch::ComputeWaterFieldDivisor(int32 ResolutionTier)
{
	if (ResolutionTier <= TDFluidGpuSolverConstants::WaterExactTierMaximumResolution)
	{
		return 1;
	}
	if (ResolutionTier < TDFluidGpuSolverConstants::WaterQuarterFieldMinimumResolution)
	{
		return BatchWaterHalfFieldDivisor;
	}
	return BatchWaterQuarterFieldDivisor;
}

int32 FTDFluidGpuBatch::ComputeWaterShiftQuantumCells(int32 ResolutionTier)
{
	return ComputeWaterFieldDivisor(ResolutionTier);
}

ETDFluidGpuWaterPressureMode FTDFluidGpuBatch::ResolveWaterPressureMode(const FTDFluidGpuSimulationSettings& Settings)
{
	if (ResolveWaterResolutionTier(Settings.Resolution) <= TDFluidGpuSolverConstants::WaterExactTierMaximumResolution)
	{
		return ETDFluidGpuWaterPressureMode::JacobiOpenBorders;
	}

	return ETDFluidGpuWaterPressureMode::HalfResolutionMultigrid;
}

int32 FTDFluidGpuBatch::ComputeWaterRippleSubstepCount(float WaveSpeedCmPerSec, float StepDeltaTimeSeconds, float SmallestCellSizeCm, int32 ResolutionTier, bool& bOutReachedCap, float& OutWaveSpeedScale)
{
	bOutReachedCap = false;
	OutWaveSpeedScale = 1.0f;
	if (WaveSpeedCmPerSec <= 0.0f)
	{
		return 0;
	}

	const float RawSubstepCount = WaveSpeedCmPerSec * StepDeltaTimeSeconds * BatchWaterSquareRootOfTwo / (TDFluidGpuSolverConstants::WaterRippleSubstepSafety * SmallestCellSizeCm);
	const int32 RoundedSubstepCount = FMath::CeilToInt(RawSubstepCount);
	if (ResolutionTier <= TDFluidGpuSolverConstants::WaterExactTierMaximumResolution)
	{
		bOutReachedCap = RoundedSubstepCount > TDFluidGpuSolverConstants::MaximumWaterRippleSubsteps;
		return FMath::Clamp(RoundedSubstepCount, 1, TDFluidGpuSolverConstants::MaximumWaterRippleSubsteps);
	}

	bOutReachedCap = RoundedSubstepCount > TDFluidGpuSolverConstants::MaximumGpuOnlyWaterRippleSubsteps;
	if (RawSubstepCount > TDFluidGpuSolverConstants::WaterRippleCourantTargetRaw)
	{
		OutWaveSpeedScale = TDFluidGpuSolverConstants::WaterRippleCourantTargetRaw / RawSubstepCount;
	}
	return FMath::Clamp(RoundedSubstepCount, 1, TDFluidGpuSolverConstants::MaximumGpuOnlyWaterRippleSubsteps);
}

void FTDFluidGpuBatch::ApplyWaterConsoleOverrides(FTDFluidGpuSimulationSettings& InOutSettings)
{
	if (!IsBatchWaterLayerSettings(InOutSettings))
	{
		return;
	}

	const bool bIsParityTier = ResolveWaterResolutionTier(InOutSettings.Resolution) <= TDFluidGpuSolverConstants::WaterExactTierMaximumResolution;
	InOutSettings.bUsesFloat32WaterVelocity = ResolveBatchWaterFloat32Request(CVarTDFluidWaterGpuVelocityFloat32.GetValueOnGameThread(), bBatchWaterVelocityAutomaticallyUsesFloat32);
	InOutSettings.bUsesFloat32Ripple = ResolveBatchWaterFloat32Request(CVarTDFluidWaterGpuRippleFloat32.GetValueOnGameThread(), bIsParityTier);
	InOutSettings.WaterPressureModeRequest = ClampBatchWaterPressureModeRequest(ResolveIntegerConsoleOverride(CVarTDFluidWaterGpuPressureMode.GetValueOnGameThread(), InOutSettings.WaterPressureModeRequest));
	InOutSettings.WaterKernelRadius = FMath::Clamp(ResolveIntegerConsoleOverride(CVarTDFluidWaterGpuKernelRadius.GetValueOnGameThread(), InOutSettings.WaterKernelRadius), BatchWaterKernelMinimumRadius, BatchWaterKernelMaximumRadius);
	InOutSettings.WaterKernelFeedback = ClampFinite(ResolveFloatConsoleOverride(CVarTDFluidWaterGpuKernelFeedback.GetValueOnGameThread(), InOutSettings.WaterKernelFeedback), 0.0f, 1.0f);
	InOutSettings.WaterKernelScale = SanitizeBatchWaterKernelScale(ResolveFloatConsoleOverride(CVarTDFluidWaterGpuKernelScale.GetValueOnGameThread(), InOutSettings.WaterKernelScale));
	InOutSettings.WaterMultigridCycles = FMath::Clamp(ResolveIntegerConsoleOverride(CVarTDFluidGpuPressureCycles.GetValueOnGameThread(), InOutSettings.WaterMultigridCycles), TDFluidGpuSolverConstants::MinimumMultigridCycles, TDFluidGpuSolverConstants::MaximumMultigridCycles);
}

int64 FTDFluidGpuBatch::ComputeWaterMultigridOpenBoundaryBytes(int32 ResolutionTier)
{
	int64 TotalBytes = 0;
	const int32 LevelCount = ComputeMultigridLevelCount(ResolutionTier);
	for (int32 Level = 1; Level < LevelCount; ++Level)
	{
		const int64 LevelResolution = ResolutionTier >> Level;
		TotalBytes += LevelResolution * LevelResolution * BatchWaterOpenBoundaryBytesPerCell;
	}
	return TotalBytes;
}

FTDFluidGpuMemoryEstimate FTDFluidGpuBatch::EstimateWaterActorMemory(const FTDFluidGpuBatchKey& WaterKey, int32 NearRippleResolution)
{
	const int32 ResolutionTier = ResolveWaterResolutionTier(WaterKey.ResolutionTier);
	const int64 Resolution = ResolutionTier;
	const int64 CellCount = Resolution * Resolution;
	const bool bUsesJacobiPressure = WaterKey.WaterPressureMode == ETDFluidGpuWaterPressureMode::JacobiOpenBorders;
	const int64 PressureResolution = bUsesJacobiPressure ? Resolution : Resolution / BatchWaterHalfPressureDivisor;
	const int64 FieldResolution = Resolution / ComputeWaterFieldDivisor(ResolutionTier);
	const int64 FieldCellCount = FieldResolution * FieldResolution;
	const int64 VelocityBytesPerTexel = WaterKey.bUsesFloat32WaterVelocity ? BatchWaterFloatPairBytesPerTexel : BatchWaterHalfPairBytesPerTexel;
	const int64 RippleBytesPerTexel = WaterKey.bUsesFloat32Ripple ? BatchWaterFloatPairBytesPerTexel : BatchWaterHalfPairBytesPerTexel;
	const int64 ReadbackTileTexelCount = static_cast<int64>(TDFluidGpuSolverConstants::ReadbackTileResolution) * TDFluidGpuSolverConstants::ReadbackTileResolution;

	FTDFluidGpuMemoryEstimate Estimate;
	Estimate.StateBytes = CellCount * (PingPongTextureCount * VelocityBytesPerTexel + DensityBytesPerCell + PingPongTextureCount * RippleBytesPerTexel + ObstacleSdfBytesPerCell + BatchWaterWetnessBytesPerCell)
		+ PressureResolution * PressureResolution * PressureSolutionBytesPerCell
		+ FieldCellCount * BatchWaterFieldBytesPerCell;
	Estimate.OutputBytes = CellCount * BatchWaterOutputBytesPerCell;
	Estimate.TransientBytes = CellCount * BatchWaterCompositeSdfBytesPerCell
		+ FieldCellCount * BatchWaterFieldDrySdfBytesPerCell
		+ ComputeBatchWaterPressureTransientBytes(WaterKey.WaterPressureMode, ResolutionTier);
	Estimate.ReadbackBytes = ReadbackTileTexelCount * RgbaHalfFloatBytes * TDFluidGpuSolverConstants::ReadbackRingSize + BatchWaterProbeReadbackBytes;
	Estimate.NearRippleStateBytes = FTDFluidGpuNearRippleState::EstimateStateBytes(NearRippleResolution);
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

	OutPreparedFrame.WaterProbeSlotCount = ComputeBatchWaterProbeSlotCount(FrameInputs);
	OutPreparedFrame.bHasWaterEntries = InOutBatches.ContainsByPredicate([](const FTDFluidGpuBatch& Batch)
	{
		return Batch.GetKey().bUsesWaterLayer && !Batch.GetFrame().Entries.IsEmpty();
	});
}

int32 FTDFluidGpuBatch::FindSlice(uint64 ActorKey) const
{
	const int32* SliceIndex = SliceByActorKey.Find(ActorKey);
	return SliceIndex ? *SliceIndex : INDEX_NONE;
}

int32 FTDFluidGpuBatch::GetRequiredSliceCapacity() const
{
	const int32 HighestOccupiedSlice = SliceOccupancy.FindLast(true);
	if (Key.ResolutionTier >= ExactSliceCapacityMinimumResolution || Key.bUsesWaterLayer)
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
	if (Key.bUsesWaterLayer)
	{
		Frame.WaterParameters.Reserve(Frame.Entries.Num());
	}

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
		StampLimits.MaxStampsPerBin = Settings.MaxStampsPerBin;
		FTDFluidGpuStampResponse StampResponse;
		StampResponse.MaxCellSpeedCmPerSec = Settings.MaxCellSpeedCmPerSec;
		StampResponse.BrushEraseRatePerSec = Settings.BrushEraseRatePerSec;
		const FTDFluidGpuStampBinSet ActorStampBins = FTDFluidGpuStampBins::Build(StampWindow, Actor.Stamps, StampLimits, StampResponse);

		const int32 FirstObstacle = Frame.Obstacles.Num();
		AppendPackedObstacles(Actor.Obstacles, WindowCenter, Frame.Obstacles);
		const int32 ObstacleCount = Frame.Obstacles.Num() - FirstObstacle;

		const ETDFluidGpuActorFlags Flags = ResolveActorFlags(Actor, Settings, PlaneUpDirection, Entry.bNeedsReset, ActorStampBins.Stamps.Num(), ObstacleCount);
		const bool bHasShift = EnumHasAnyFlags(Flags, ETDFluidGpuActorFlags::HasShift);

		FTDFluidGpuActorParameters& Parameters = Frame.Parameters.Add_GetRef(MakeActorParameters(Actor, Settings, WindowCenter, PlaneUpDirection, Key.ResolutionTier));
		Parameters.SliceIndex = Entry.SliceIndex;
		Parameters.Flags = static_cast<uint32>(Flags);
		if (Key.MassInputDivisor > 0)
		{
			Parameters.Flags |= static_cast<uint32>(ETDFluidGpuActorFlags::ConsumesMassInput);
		}
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
		if (Key.bUsesWaterLayer)
		{
			AppendBatchWaterEntry(Key, EntryIndex, Entry, Actor, Settings, Frame);
		}
	}
}
