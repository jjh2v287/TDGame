#include "FluidFX/TDFluidPresetAsset.h"

FTDFluidSimulationConfig UTDFluidPresetAsset::ResolveEffectiveConfig() const
{
	if (PresetKind == ETDFluidPresetKind::Custom || bOverridePresetDefaults)
	{
		return SimulationConfig;
	}
	return BuildConfigForPresetKind(PresetKind);
}

FTDFluidSimulationConfig UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind Kind)
{
	FTDFluidSimulationConfig Config;

	switch (Kind)
	{
	case ETDFluidPresetKind::Water:
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = 64;
		Config.SimExtentCm = FVector2D(4000.0f, 4000.0f);
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = 0.20f;
		Config.PressureIterations = 8;
		Config.VorticityStrength = 0.8f;
		Config.TerrainSlopeGravity = 600.0f;
		Config.AdvectionSpeedScale = 1.2f;
		Config.BorderFadeRatio = 0.1f;
		Config.MaxPointsPerTick = 64;
		break;

	case ETDFluidPresetKind::Mud:
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = 64;
		Config.SimExtentCm = FVector2D(3200.0f, 3200.0f);
		Config.VelocityDamping = 0.65f;
		Config.DensityDissipation = 0.01f;
		Config.PressureIterations = 6;
		Config.VorticityStrength = 0.15f;
		Config.TerrainSlopeGravity = 120.0f;
		Config.AdvectionSpeedScale = 0.35f;
		Config.BorderFadeRatio = 0.1f;
		Config.MaxPointsPerTick = 64;
		break;

	case ETDFluidPresetKind::Blood:
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = 64;
		Config.SimExtentCm = FVector2D(2400.0f, 2400.0f);
		Config.VelocityDamping = 0.45f;
		Config.DensityDissipation = 0.01f;
		Config.PressureIterations = 6;
		Config.VorticityStrength = 0.3f;
		Config.TerrainSlopeGravity = 450.0f;
		Config.AdvectionSpeedScale = 0.55f;
		Config.BorderFadeRatio = 0.12f;
		Config.MaxPointsPerTick = 48;
		break;

	case ETDFluidPresetKind::Fire:
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = 64;
		Config.SimExtentCm = FVector2D(2800.0f, 2800.0f);
		Config.VelocityDamping = 0.15f;
		Config.DensityDissipation = 0.45f;
		Config.PressureIterations = 6;
		Config.VorticityStrength = 2.5f;
		Config.TerrainSlopeGravity = 200.0f;
		Config.AdvectionSpeedScale = 1.35f;
		Config.BorderFadeRatio = 0.15f;
		Config.MaxPointsPerTick = 64;
		break;

	case ETDFluidPresetKind::FootprintPainter:
		Config.SimMode = ETDFluidSimMode::SimplePainter;
		Config.GridResolution = 64;
		Config.SimExtentCm = FVector2D(4000.0f, 4000.0f);
		Config.VelocityDamping = 1.0f;
		Config.DensityDissipation = 0.01f;
		Config.PressureIterations = 1;
		Config.VorticityStrength = 0.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 0.0f;
		Config.BorderFadeRatio = 0.1f;
		Config.MaxPointsPerTick = 64;
		break;

	case ETDFluidPresetKind::Smoke:
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.GridResolution = 256;
		Config.SimExtentCm = FVector2D(700.0f, 700.0f);
		Config.VelocityDamping = 0.08f;
		Config.DensityDissipation = 0.0f;
		Config.VorticityStrength = 1.2f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AmbientDensity = 0.8f;
		Config.AmbientRefillPerSec = 0.5f;
		Config.ActorBrushDensityStrength = -1.0f;
		Config.ActorBrushPunctureCmPerSec = 380.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 70.0f;
		Config.FlowNoiseScaleCm = 220.0f;
		Config.FlowNoiseSpeed = 0.12f;
		Config.AmbientNoiseAmount = 0.45f;
		break;

	case ETDFluidPresetKind::Custom:
	default:
		break;
	}

	return Config;
}

UTDFluidPresetAsset* UTDFluidPresetAsset::CreateTransientPreset(UObject* Outer, ETDFluidPresetKind Kind)
{
	UObject* ResolvedOuter = Outer ? Outer : GetTransientPackage();
	UTDFluidPresetAsset* Preset = NewObject<UTDFluidPresetAsset>(ResolvedOuter);
	Preset->PresetKind = Kind;
	Preset->SimulationConfig = BuildConfigForPresetKind(Kind);

	switch (Kind)
	{
	case ETDFluidPresetKind::Mud:
	case ETDFluidPresetKind::FootprintPainter:
		Preset->DefaultChannel = ETDFluidBrushChannel::ChannelR_Mud;
		break;
	case ETDFluidPresetKind::Blood:
		Preset->DefaultChannel = ETDFluidBrushChannel::ChannelG_Blood;
		break;
	case ETDFluidPresetKind::Fire:
		Preset->DefaultChannel = ETDFluidBrushChannel::ChannelB_Fire;
		break;
	case ETDFluidPresetKind::Water:
	case ETDFluidPresetKind::Custom:
	default:
		Preset->DefaultChannel = ETDFluidBrushChannel::ChannelA_Water;
		break;
	}

	return Preset;
}
