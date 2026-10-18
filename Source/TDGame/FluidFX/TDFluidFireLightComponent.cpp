#include "FluidFX/TDFluidFireLightComponent.h"
#include "FluidFX/TDFluidBandLimitedNoise.h"
#include "FluidFX/TDFluidLiveComponent.h"

namespace
{
	constexpr float DefaultAttenuationRadiusCm = 800.0f;
	constexpr float DefaultSourceRadiusCm = 15.0f;
	constexpr float VisibleIntensityThresholdCandelas = 0.01f;
}

UTDFluidFireLightComponent::UTDFluidFireLightComponent()
{
	SetMobility(EComponentMobility::Movable);
	IntensityUnits = ELightUnits::Candelas;
	AttenuationRadius = DefaultAttenuationRadiusCm;
	SourceRadius = DefaultSourceRadiusCm;
	Intensity = 0.0f;
}

void UTDFluidFireLightComponent::UpdateFromFluid(const UTDFluidLiveComponent& LiveComponent, const FVector& AnchorWorld, float DeltaTime)
{
	if (!bIsDrivenByFluid || DeltaTime <= 0.0f)
	{
		return;
	}

	ElapsedSeconds += DeltaTime;

	FVector2D DensityCentroidPlane = FVector2D::ZeroVector;
	float DensityAreaCm2 = 0.0f;
	const bool bHasDensity = LiveComponent.ComputeDensityCentroidPlane(bUsesSelectiveDensity, DensityCentroidPlane, DensityAreaCm2);
	LastDensityAreaCm2 = DensityAreaCm2;

	const float TargetIntensity = MaxIntensityCandelas * (1.0f - FMath::Exp(-DensityAreaCm2 / FMath::Max(ReferenceDensityAreaCm2, 1.0f)));
	LastTargetIntensity = TargetIntensity;

	const float FlickerNoise = TDFluidDeterministic::EvaluateBandLimitedNoise(RandomSeed, 0, ElapsedSeconds, FlickerNoiseFrequencyHz);
	const float NoisyTarget = TargetIntensity * FMath::Max(0.0f, 1.0f + FlickerNoiseAmplitude * FlickerNoise);
	const float ResponseBlend = 1.0f - FMath::Exp(-ResponsePerSec * DeltaTime);
	LastIntensityCandelas += (NoisyTarget - LastIntensityCandelas) * ResponseBlend;

	const float HeatRatio = MaxIntensityCandelas > UE_SMALL_NUMBER ? FMath::Clamp(TargetIntensity / MaxIntensityCandelas, 0.0f, 1.0f) : 0.0f;
	const float ColorTemperatureKelvin = FMath::Lerp(MinColorTemperatureKelvin, MaxColorTemperatureKelvin, HeatRatio);
	SetLightColor(FLinearColor::MakeFromColorTemperature(ColorTemperatureKelvin), false);
	SetIntensity(LastIntensityCandelas);
	SetVisibility(LastIntensityCandelas > VisibleIntensityThresholdCandelas);

	ApplyLightPosition(LiveComponent, AnchorWorld, DensityCentroidPlane, bHasDensity);
}

void UTDFluidFireLightComponent::ApplyLightPosition(const UTDFluidLiveComponent& LiveComponent, const FVector& AnchorWorld, const FVector2D& DensityCentroidPlane, bool bHasDensity)
{
	const FTDFluidPlaneBasis& PlaneBasis = LiveComponent.GetPlaneBasis();
	const FVector2D AnchorPlane = LiveComponent.ProjectWorldToSolverPlane(AnchorWorld);
	const FVector2D CentroidOffsetPlane = bHasDensity ? DensityCentroidPlane - AnchorPlane : FVector2D::ZeroVector;
	const double VerticalOffsetCm = CentroidOffsetPlane.Y * PlaneBasis.V.Z + HeightOffsetCm;
	const FVector TargetLocation = AnchorWorld + PlaneBasis.U * CentroidOffsetPlane.X + FVector(0.0, 0.0, VerticalOffsetCm);
	if (GetComponentLocation().Equals(TargetLocation, MoveThresholdCm))
	{
		return;
	}

	SetWorldLocation(TargetLocation);
}
