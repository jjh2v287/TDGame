#include "FluidFX/TDFluidSolverOptionRules.h"

#include "HAL/IConsoleManager.h"
#include "Misc/Crc.h"

static TAutoConsoleVariable<int32> CVarTDFluidCpuPressureMethod(
	TEXT("TD.FluidFX.CpuPressureMethod"),
	-1,
	TEXT("CPU fluid pressure method override. -1: preset, 0: Poisson solve, 1: kernel feedback (square, not wrapped, not water; otherwise Poisson)."),
	ECVF_Default);

namespace TDFluidSolverOptionRulesPrivate
{
	constexpr double MaximumDirectVelocityCmPerSec = 1200.0;
	constexpr float MaximumFlowAmplitudeNoiseDampingPerSec = 20.0f;
	constexpr float MinimumFlowAmplitudeNoiseScaleCm = 1.0f;
	constexpr float MaximumFlowAmplitudeNoiseScaleCm = 100000.0f;
	constexpr float DefaultFlowAmplitudeNoiseScaleCm = 200.0f;
	constexpr float MinimumAdvectionSpeedScale = 0.01f;
	constexpr int32 NoiseLatticeOffsetMask = 1023;
	constexpr int32 NoiseLatticeOffsetBitsPerAxis = 10;

	float ClampOptionValue(float Value, float Minimum, float Maximum, float FallbackValue)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : FallbackValue;
	}

	double ClampDirectVelocityComponent(double Value)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, -MaximumDirectVelocityCmPerSec, MaximumDirectVelocityCmPerSec) : 0.0;
	}

	FVector SanitizeDirectVelocity(const FVector& DirectVelocityCmPerSec)
	{
		return FVector(
			ClampDirectVelocityComponent(DirectVelocityCmPerSec.X),
			ClampDirectVelocityComponent(DirectVelocityCmPerSec.Y),
			ClampDirectVelocityComponent(DirectVelocityCmPerSec.Z));
	}

	bool IsWrappedWindow(const FTDFluidSimulationConfig& Config)
	{
		return Config.AreaAddressMode == ETDFluidAreaAddressMode::Wrap;
	}

	bool AllowsSolverOptions(const FTDFluidSimulationConfig& Config)
	{
		if (IsWrappedWindow(Config))
		{
			return true;
		}
		return Config.SimMode == ETDFluidSimMode::FluidSim && !Config.bEnableWaterLayer;
	}

	FVector2D ComputeWindowVelocity(const FTDFluidAdvectionOffsetInputs& Inputs, double MaxCellSpeedCmPerSec)
	{
		if (!Inputs.bHasPreviousCenter || !(Inputs.DeltaTimeSeconds > 0.0f))
		{
			return FVector2D::ZeroVector;
		}

		const FVector2D CenterMotion = Inputs.CurrentCenterPlane - Inputs.PreviousCenterPlane;
		const double TeleportDistanceCm = 0.5 * FMath::Min(Inputs.Config->SimExtentCm.X, Inputs.Config->SimExtentCm.Y);
		if (CenterMotion.ContainsNaN() || CenterMotion.Size() > TeleportDistanceCm)
		{
			return FVector2D::ZeroVector;
		}

		const FVector2D WindowVelocity = CenterMotion / static_cast<double>(Inputs.DeltaTimeSeconds);
		if (WindowVelocity.Size() > MaxCellSpeedCmPerSec)
		{
			return FVector2D::ZeroVector;
		}
		return WindowVelocity;
	}
}

namespace TDFluidSolverOptionRules
{
	void SanitizeSolverOptions(FTDFluidSimulationConfig& InOutConfig)
	{
		using namespace TDFluidSolverOptionRulesPrivate;

		InOutConfig.PressureKernelDivergenceRadiusTexels = FMath::Clamp(InOutConfig.PressureKernelDivergenceRadiusTexels, TDFluidGpuSolverOptionConstants::MinimumKernelDivergenceRadiusTexels, TDFluidGpuSolverOptionConstants::MaximumKernelRadiusTexels);
		InOutConfig.PressureKernelPassesPerProjection = FMath::Clamp(InOutConfig.PressureKernelPassesPerProjection, 1, TDFluidGpuSolverOptionConstants::MaximumKernelPassesPerProjection);
		InOutConfig.DirectVelocityCmPerSec = SanitizeDirectVelocity(InOutConfig.DirectVelocityCmPerSec);
		InOutConfig.SimAreaMotionVelocityScale = ClampOptionValue(InOutConfig.SimAreaMotionVelocityScale, 0.0f, 1.0f, 0.0f);
		InOutConfig.FlowAmplitudeNoiseDampingPerSec = ClampOptionValue(InOutConfig.FlowAmplitudeNoiseDampingPerSec, 0.0f, MaximumFlowAmplitudeNoiseDampingPerSec, 0.0f);
		InOutConfig.FlowAmplitudeNoiseScaleCm = ClampOptionValue(InOutConfig.FlowAmplitudeNoiseScaleCm, MinimumFlowAmplitudeNoiseScaleCm, MaximumFlowAmplitudeNoiseScaleCm, DefaultFlowAmplitudeNoiseScaleCm);

		if (!AllowsSolverOptions(InOutConfig))
		{
			InOutConfig.DirectVelocityCmPerSec = FVector::ZeroVector;
			InOutConfig.FlowAmplitudeNoiseDampingPerSec = 0.0f;
			InOutConfig.bAnchorsDensityToActor = false;
			InOutConfig.bRandomizeNoiseOffsetPerActor = false;
		}

		if (IsWrappedWindow(InOutConfig))
		{
			InOutConfig.bAnchorsDensityToActor = false;
		}

		if (!InOutConfig.bAnchorsDensityToActor)
		{
			InOutConfig.SimAreaMotionVelocityScale = 0.0f;
			return;
		}

		InOutConfig.TerrainSlopeGravity = 0.0f;
	}

	ETDFluidPressureMethod ResolveCpuPressureMethod(const FTDFluidSimulationConfig& Config)
	{
		const int32 ConsolePressureMethod = CVarTDFluidCpuPressureMethod.GetValueOnAnyThread();
		if (ConsolePressureMethod < 0)
		{
			return Config.PressureMethod;
		}
		return ConsolePressureMethod == 0 ? ETDFluidPressureMethod::PoissonSolve : ETDFluidPressureMethod::KernelFeedback;
	}

	bool CanUseKernelPressure(const FTDFluidSimulationConfig& Config)
	{
		if (Config.SimMode != ETDFluidSimMode::FluidSim || Config.bEnableWaterLayer || Config.AreaAddressMode != ETDFluidAreaAddressMode::Clamp)
		{
			return false;
		}

		const double ExtentX = Config.SimExtentCm.X;
		const double ExtentY = Config.SimExtentCm.Y;
		return FMath::Abs(ExtentX - ExtentY) <= 1.0e-3 * FMath::Max(ExtentX, ExtentY);
	}

	FTDFluidGpuSolverOptionSettings MakeGpuSolverOptionSettings(const FTDFluidSimulationConfig& Config)
	{
		FTDFluidGpuSolverOptionSettings Settings;
		Settings.KernelRadiusTexels = Config.PressureKernelRadiusTexels;
		Settings.KernelDivergenceRadiusTexels = Config.PressureKernelDivergenceRadiusTexels;
		Settings.KernelPassesPerProjection = Config.PressureKernelPassesPerProjection;
		Settings.KernelFeedback = Config.PressureKernelFeedback;
		Settings.KernelScale = Config.PressureKernelScale;
		Settings.FlowAmplitudeNoiseDampingPerSec = Config.FlowAmplitudeNoiseDampingPerSec;
		Settings.FlowAmplitudeNoiseScaleCm = Config.FlowAmplitudeNoiseScaleCm;
		return Settings;
	}

	FVector2D ComputeAdvectionOffsetPlane(const FTDFluidAdvectionOffsetInputs& Inputs)
	{
		using namespace TDFluidSolverOptionRulesPrivate;

		if (Inputs.Config == nullptr || !AllowsSolverOptions(*Inputs.Config))
		{
			return FVector2D::ZeroVector;
		}

		const FTDFluidSimulationConfig& Config = *Inputs.Config;
		const FVector DirectVelocity = SanitizeDirectVelocity(Config.DirectVelocityCmPerSec);
		const FVector2D PlaneWind = Config.bDirectVelocityInWorldSpace ? Inputs.PlaneBasis.ProjectVector(DirectVelocity) : FVector2D(DirectVelocity.X, DirectVelocity.Y);
		const double MaxCellSpeedCmPerSec = FMath::IsFinite(Config.MaxCellSpeedCmPerSec) ? FMath::Max(static_cast<double>(Config.MaxCellSpeedCmPerSec), 0.0) : 0.0;
		const double MotionScale = IsDensityAnchoredToActor(Config) ? ClampOptionValue(Config.SimAreaMotionVelocityScale, 0.0f, 1.0f, 0.0f) : 0.0;
		const FVector2D WindowVelocity = MotionScale > 0.0 ? ComputeWindowVelocity(Inputs, MaxCellSpeedCmPerSec) : FVector2D::ZeroVector;
		const FVector2D SolverOffset = (PlaneWind - MotionScale * WindowVelocity).GetClampedToMaxSize(MaxCellSpeedCmPerSec);
		const double AdvectionSpeedScale = FMath::IsFinite(Config.AdvectionSpeedScale) ? FMath::Max(Config.AdvectionSpeedScale, MinimumAdvectionSpeedScale) : 1.0;
		return SolverOffset / AdvectionSpeedScale;
	}

	FIntPoint ComputeNoiseLatticeOffsetCells(const FVector& SpawnLocationCm, ETDFluidPresetKind Kind, const FString& OwnerName)
	{
		using namespace TDFluidSolverOptionRulesPrivate;

		const int32 HashInputs[4] = {
			static_cast<int32>(FMath::RoundToInt(SpawnLocationCm.X)),
			static_cast<int32>(FMath::RoundToInt(SpawnLocationCm.Y)),
			static_cast<int32>(FMath::RoundToInt(SpawnLocationCm.Z)),
			static_cast<int32>(Kind)
		};
		const uint32 Hash = FCrc::MemCrc32(HashInputs, static_cast<int32>(sizeof(HashInputs))) ^ FCrc::StrCrc32(*OwnerName);
		return FIntPoint(static_cast<int32>(Hash & NoiseLatticeOffsetMask), static_cast<int32>((Hash >> NoiseLatticeOffsetBitsPerAxis) & NoiseLatticeOffsetMask));
	}

	bool IsDensityAnchoredToActor(const FTDFluidSimulationConfig& Config)
	{
		using namespace TDFluidSolverOptionRulesPrivate;

		return Config.bAnchorsDensityToActor && !IsWrappedWindow(Config) && AllowsSolverOptions(Config);
	}

	bool IsNoiseLatticeOffsetEnabled(const FTDFluidSimulationConfig& Config)
	{
		using namespace TDFluidSolverOptionRulesPrivate;

		return Config.bRandomizeNoiseOffsetPerActor && AllowsSolverOptions(Config);
	}
}
