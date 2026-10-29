#include "TDFluidGpuSolverOptions.h"

namespace TDFluidGpuSolverOptionsPrivate
{
	constexpr float MaximumFlowAmplitudeNoiseDampingPerSec = 20.0f;
	constexpr float MinimumFlowAmplitudeNoiseScaleCm = 1.0f;
	constexpr float MaximumFlowAmplitudeNoiseScaleCm = 100000.0f;
	constexpr float DefaultFlowAmplitudeNoiseScaleCm = 200.0f;

	struct FTDFluidSolverOptionLatticePlacement
	{
		FIntPoint Origin = FIntPoint::ZeroValue;
		FVector2f Offset = FVector2f::ZeroVector;
		FVector2f StepPerCell = FVector2f::ZeroVector;
		uint32 PeriodPacked = 0;
	};

	float ClampOptionValue(float Value, float Minimum, float Maximum, float FallbackValue)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : FallbackValue;
	}

	bool IsWrappedSolverOptionWindow(const FTDFluidGpuSimulationSettings& Settings)
	{
		return Settings.AreaAddressModeRequest == TDFluidGpuSolverConstants::WrapAreaAddressModeRequest;
	}

	bool AllowsSolverOptions(const FTDFluidGpuSimulationSettings& Settings)
	{
		if (IsWrappedSolverOptionWindow(Settings))
		{
			return true;
		}
		return Settings.SimMode == ETDFluidGpuSimMode::FluidSim && !Settings.bUsesWaterLayer;
	}

	int32 ResolveSolverOptionPeriodicLatticeCount(double ExtentCm, float SpacingCm)
	{
		return FMath::Max(1, FMath::RoundToInt(static_cast<float>(ExtentCm) / SpacingCm));
	}

	FTDFluidSolverOptionLatticePlacement MakeSolverOptionLatticePlacement(const FTDFluidGpuSimulationSettings& Settings, const FVector2D& WindowCenter, int32 Resolution, float SpacingCm)
	{
		FTDFluidSolverOptionLatticePlacement Placement;
		if (IsWrappedSolverOptionWindow(Settings))
		{
			const int32 PeriodX = ResolveSolverOptionPeriodicLatticeCount(Settings.ExtentCm.X, SpacingCm);
			const int32 PeriodY = ResolveSolverOptionPeriodicLatticeCount(Settings.ExtentCm.Y, SpacingCm);
			Placement.StepPerCell = FVector2f(
				static_cast<float>(static_cast<double>(PeriodX) / Resolution),
				static_cast<float>(static_cast<double>(PeriodY) / Resolution));
			Placement.PeriodPacked = static_cast<uint32>(PeriodX) | (static_cast<uint32>(PeriodY) << 16);
			return Placement;
		}

		const double InverseSpacing = 1.0 / static_cast<double>(SpacingCm);
		const double LatticeStartX = (WindowCenter.X - 0.5 * Settings.ExtentCm.X) * InverseSpacing;
		const double LatticeStartY = (WindowCenter.Y - 0.5 * Settings.ExtentCm.Y) * InverseSpacing;
		const double OriginX = FMath::FloorToDouble(LatticeStartX);
		const double OriginY = FMath::FloorToDouble(LatticeStartY);
		Placement.Origin = FIntPoint(static_cast<int32>(OriginX), static_cast<int32>(OriginY));
		Placement.Offset = FVector2f(static_cast<float>(LatticeStartX - OriginX), static_cast<float>(LatticeStartY - OriginY));
		Placement.StepPerCell = FVector2f(
			static_cast<float>(Settings.ExtentCm.X / Resolution * InverseSpacing),
			static_cast<float>(Settings.ExtentCm.Y / Resolution * InverseSpacing));
		return Placement;
	}
}

FTDFluidGpuSolverOptionParameters MakeTDFluidGpuSolverOptionParameters(const FTDFluidGpuSimulationSettings& Settings, const FVector2D& WindowCenter, float StepDeltaTimeSeconds, int32 Resolution)
{
	const FTDFluidGpuSolverOptionSettings& Options = Settings.SolverOptions;
	const int32 KernelRadiusTexels = FMath::Clamp(Options.KernelRadiusTexels, TDFluidGpuSolverOptionConstants::MinimumKernelRadiusTexels, TDFluidGpuSolverOptionConstants::MaximumKernelRadiusTexels);
	const int32 KernelDivergenceRadiusTexels = FMath::Clamp(Options.KernelDivergenceRadiusTexels, TDFluidGpuSolverOptionConstants::MinimumKernelDivergenceRadiusTexels, TDFluidGpuSolverOptionConstants::MaximumKernelRadiusTexels);

	FTDFluidGpuSolverOptionParameters Parameters;
	Parameters.KernelRadiusTexels = static_cast<uint32>(KernelRadiusTexels);
	Parameters.KernelDivergenceRadiusTexels = static_cast<uint32>(KernelDivergenceRadiusTexels);
	Parameters.KernelFeedback = FMath::Clamp(Options.KernelFeedback, 0.0f, 1.0f);
	Parameters.KernelDivergenceScale = ComputeTDFluidKernelDivergenceScale(KernelRadiusTexels, FMath::Max(Options.KernelScale, 0.0f));
	if (!TDFluidGpuSolverOptionsPrivate::AllowsSolverOptions(Settings) || Resolution <= 0)
	{
		return Parameters;
	}

	const float DampingPerSec = TDFluidGpuSolverOptionsPrivate::ClampOptionValue(Options.FlowAmplitudeNoiseDampingPerSec, 0.0f, TDFluidGpuSolverOptionsPrivate::MaximumFlowAmplitudeNoiseDampingPerSec, 0.0f);
	const float SafeStepDeltaTimeSeconds = FMath::IsFinite(StepDeltaTimeSeconds) ? FMath::Max(StepDeltaTimeSeconds, 0.0f) : 0.0f;
	if (DampingPerSec > 0.0f)
	{
		const float ScaleCm = TDFluidGpuSolverOptionsPrivate::ClampOptionValue(Options.FlowAmplitudeNoiseScaleCm, TDFluidGpuSolverOptionsPrivate::MinimumFlowAmplitudeNoiseScaleCm, TDFluidGpuSolverOptionsPrivate::MaximumFlowAmplitudeNoiseScaleCm, TDFluidGpuSolverOptionsPrivate::DefaultFlowAmplitudeNoiseScaleCm);
		const float LargestCellSizeCm = static_cast<float>(FMath::Max(Settings.ExtentCm.X, Settings.ExtentCm.Y)) / static_cast<float>(Resolution);
		const TDFluidGpuSolverOptionsPrivate::FTDFluidSolverOptionLatticePlacement Placement = TDFluidGpuSolverOptionsPrivate::MakeSolverOptionLatticePlacement(Settings, WindowCenter, Resolution, FMath::Max(ScaleCm, LargestCellSizeCm));
		Parameters.OptionFlags |= static_cast<uint32>(ETDFluidGpuSolverOptionFlags::UsesFlowAmplitudeNoise);
		Parameters.FlowAmplitudeDampingStep = DampingPerSec * SafeStepDeltaTimeSeconds;
		Parameters.AmplitudeNoiseLatticeOrigin = Placement.Origin + Options.NoiseLatticeOffsetCells;
		Parameters.AmplitudeNoiseLatticeOffset = Placement.Offset;
		Parameters.AmplitudeNoiseLatticeStepPerCell = Placement.StepPerCell;
		Parameters.AmplitudeNoiseLatticePeriodPacked = Placement.PeriodPacked;
	}

	const FVector2D AdvectionOffset = Options.AdvectionOffsetPlaneCmPerSec;
	if (!AdvectionOffset.ContainsNaN() && !AdvectionOffset.IsZero())
	{
		Parameters.OptionFlags |= static_cast<uint32>(ETDFluidGpuSolverOptionFlags::UsesAdvectionOffset);
		Parameters.AdvectionOffsetCmPerSec = FVector2f(AdvectionOffset);
	}
	return Parameters;
}

bool CanTDFluidGpuUseKernelPressure(const FTDFluidGpuSimulationSettings& Settings)
{
	if (Settings.SimMode != ETDFluidGpuSimMode::FluidSim || Settings.bUsesWaterLayer || Settings.AreaAddressModeRequest == TDFluidGpuSolverConstants::WrapAreaAddressModeRequest)
	{
		return false;
	}

	const double ExtentX = Settings.ExtentCm.X;
	const double ExtentY = Settings.ExtentCm.Y;
	return FMath::Abs(ExtentX - ExtentY) <= 1.0e-3 * FMath::Max(ExtentX, ExtentY);
}

float ComputeTDFluidKernelWeight(int32 Offset, int32 RadiusTexels)
{
	if (RadiusTexels <= 0)
	{
		return Offset == 0 ? 1.0f : 0.0f;
	}

	const float OffsetAsFloat = static_cast<float>(Offset);
	const float RadiusAsFloat = static_cast<float>(RadiusTexels);
	return FMath::Exp(-2.0f * OffsetAsFloat * OffsetAsFloat / (RadiusAsFloat * RadiusAsFloat));
}

float ComputeTDFluidKernelDivergenceScale(int32 RadiusTexels, float Scale)
{
	if (RadiusTexels <= 0)
	{
		return 0.0f;
	}

	double WeightSum = 0.0;
	double MomentSum = 0.0;
	for (int32 Offset = -RadiusTexels; Offset <= RadiusTexels; ++Offset)
	{
		const double Weight = ComputeTDFluidKernelWeight(Offset, RadiusTexels);
		WeightSum += Weight;
		MomentSum += Weight * static_cast<double>(Offset * Offset);
	}
	return static_cast<float>(0.5 * (MomentSum / WeightSum) * static_cast<double>(Scale));
}

bool DoesTDFluidGpuFrameUseSolverOption(const FTDFluidGpuBatchFrame& Frame, ETDFluidGpuSolverOptionFlags Flag)
{
	for (const FTDFluidGpuSolverOptionParameters& Parameters : Frame.SolverOptionParameters)
	{
		if (EnumHasAnyFlags(static_cast<ETDFluidGpuSolverOptionFlags>(Parameters.OptionFlags), Flag))
		{
			return true;
		}
	}
	return false;
}
