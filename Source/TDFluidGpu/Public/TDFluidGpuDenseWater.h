#pragma once

#include "CoreMinimal.h"

struct FTDFluidGpuDenseWaterSettings
{
	float GravityCmPerSec2 = 980.0f;
	float GapFillTendency = 1.0f;
	float PeakAvoidance = 1.0f;
	float PeakSoftnessCm = 2.0f;
	float SdfRepelCmPerSec2 = 400.0f;
	float SdfRepelBandCm = 30.0f;
	float LinearDragPerSec = 0.3f;
	float BedFriction = 0.01f;
	float WetThresholdCm = 1.0f;
	float DryDepthCm = 0.05f;
	float FilmDrainCmPerSec = 0.2f;
	float MaxFlowDepthCm = 200.0f;
	float MaxSpeedCmPerSec = 400.0f;
	float CourantNumber = 0.5f;
	int32 MaxSubsteps = 4;
	float MomentumAdvection = 1.0f;
	float ReservoirRelaxPerSec = 8.0f;
};

struct FTDFluidGpuDenseWaterStepPlan
{
	int32 SubstepCount = 1;
	float SubstepSeconds = 0.0f;
	float GravityScale = 1.0f;
	float MaxSpeedCmPerSec = 0.0f;
	bool bReachedCap = false;
};

struct FTDFluidGpuDenseWaterRules
{
	static constexpr int32 HalfResolutionTier = 512;
	static constexpr float GravityWaveCourantLimit = 0.5f;

	static void Sanitize(FTDFluidGpuDenseWaterSettings& InOutSettings)
	{
		InOutSettings.GravityCmPerSec2 = FMath::Clamp(InOutSettings.GravityCmPerSec2, 50.0f, 3000.0f);
		InOutSettings.GapFillTendency = FMath::Clamp(InOutSettings.GapFillTendency, 0.0f, 2.0f);
		InOutSettings.PeakAvoidance = FMath::Clamp(InOutSettings.PeakAvoidance, 0.0f, 1.0f);
		InOutSettings.PeakSoftnessCm = FMath::Clamp(InOutSettings.PeakSoftnessCm, 0.1f, 50.0f);
		InOutSettings.SdfRepelCmPerSec2 = FMath::Clamp(InOutSettings.SdfRepelCmPerSec2, 0.0f, 5000.0f);
		InOutSettings.SdfRepelBandCm = FMath::Clamp(InOutSettings.SdfRepelBandCm, 1.0f, 500.0f);
		InOutSettings.LinearDragPerSec = FMath::Clamp(InOutSettings.LinearDragPerSec, 0.0f, 10.0f);
		InOutSettings.BedFriction = FMath::Clamp(InOutSettings.BedFriction, 0.0f, 1.0f);
		InOutSettings.WetThresholdCm = FMath::Clamp(InOutSettings.WetThresholdCm, 0.01f, 50.0f);
		InOutSettings.DryDepthCm = FMath::Clamp(InOutSettings.DryDepthCm, 0.001f, 5.0f);
		InOutSettings.FilmDrainCmPerSec = FMath::Clamp(InOutSettings.FilmDrainCmPerSec, 0.0f, 10.0f);
		InOutSettings.MaxFlowDepthCm = FMath::Clamp(InOutSettings.MaxFlowDepthCm, 10.0f, 2000.0f);
		InOutSettings.MaxSpeedCmPerSec = FMath::Clamp(InOutSettings.MaxSpeedCmPerSec, 10.0f, 2000.0f);
		InOutSettings.CourantNumber = FMath::Clamp(InOutSettings.CourantNumber, 0.1f, 0.9f);
		InOutSettings.MaxSubsteps = FMath::Clamp(InOutSettings.MaxSubsteps, 1, 16);
		InOutSettings.MomentumAdvection = FMath::Clamp(InOutSettings.MomentumAdvection, 0.0f, 1.0f);
		InOutSettings.ReservoirRelaxPerSec = FMath::Clamp(InOutSettings.ReservoirRelaxPerSec, 0.0f, 100.0f);
	}

	static int32 ResolveDenseDivisor(int32 Tier)
	{
		return Tier >= HalfResolutionTier ? 2 : 1;
	}

	static FTDFluidGpuDenseWaterStepPlan PlanStep(const FTDFluidGpuDenseWaterSettings& Settings, float DeltaSeconds, float SmallestDenseCellCm)
	{
		FTDFluidGpuDenseWaterStepPlan Plan;
		Plan.SubstepSeconds = FMath::Max(DeltaSeconds, 0.0f);
		Plan.MaxSpeedCmPerSec = Settings.MaxSpeedCmPerSec;
		const float CourantNumber = Settings.CourantNumber;
		if (DeltaSeconds <= 0.0f || SmallestDenseCellCm <= 0.0f || CourantNumber <= 0.0f)
		{
			return Plan;
		}

		const int32 MaxSubsteps = FMath::Max(Settings.MaxSubsteps, 1);
		const float GravityDepth = FMath::Max(Settings.GravityCmPerSec2 * Settings.MaxFlowDepthCm, 0.0f);
		const float SignalSpeed = FMath::Sqrt(GravityDepth) + FMath::Max(Settings.MaxSpeedCmPerSec, 0.0f);
		const float RequiredSubsteps = FMath::CeilToFloat(DeltaSeconds * SignalSpeed / (CourantNumber * SmallestDenseCellCm));
		Plan.bReachedCap = RequiredSubsteps > static_cast<float>(MaxSubsteps);
		Plan.SubstepCount = FMath::Clamp(static_cast<int32>(FMath::Min(RequiredSubsteps, static_cast<float>(MaxSubsteps))), 1, MaxSubsteps);
		Plan.SubstepSeconds = DeltaSeconds / static_cast<float>(Plan.SubstepCount);
		if (Plan.bReachedCap && GravityDepth > 0.0f)
		{
			const float WaveSpeedLimit = GravityWaveCourantLimit * SmallestDenseCellCm / Plan.SubstepSeconds;
			Plan.GravityScale = FMath::Min(1.0f, WaveSpeedLimit * WaveSpeedLimit / GravityDepth);
		}
		Plan.MaxSpeedCmPerSec = FMath::Min(Settings.MaxSpeedCmPerSec, CourantNumber * SmallestDenseCellCm / Plan.SubstepSeconds);
		return Plan;
	}

	static float ComputePeakAvoidanceFactor(float UpwindSurfaceCm, float DownwindTerrainCm, float PeakAvoidance, float SoftnessCm)
	{
		const float RiseAboveSurfaceCm = DownwindTerrainCm - UpwindSurfaceCm;
		if (RiseAboveSurfaceCm <= 0.0f)
		{
			return 1.0f;
		}

		const float BlockedFraction = FMath::Clamp(RiseAboveSurfaceCm / FMath::Max(SoftnessCm, UE_KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		return 1.0f - FMath::Clamp(PeakAvoidance, 0.0f, 1.0f) * BlockedFraction;
	}

	static float ComputeOutflowScale(float OutgoingSpeedSumPerCm, float SubstepSeconds)
	{
		const float OutflowFraction = OutgoingSpeedSumPerCm * SubstepSeconds;
		return OutflowFraction > 1.0f ? 1.0f / OutflowFraction : 1.0f;
	}

	static float ComputeReservoirBlend(float RelaxPerSec, float SubstepSeconds)
	{
		return 1.0f - FMath::Exp(-FMath::Max(RelaxPerSec, 0.0f) * FMath::Max(SubstepSeconds, 0.0f));
	}
};
