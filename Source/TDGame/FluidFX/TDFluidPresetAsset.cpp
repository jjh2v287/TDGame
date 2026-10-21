#include "FluidFX/TDFluidPresetAsset.h"

namespace
{
	void ApplyWaterLayerBaseConfig(FTDFluidSimulationConfig& Config)
	{
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.bEnableWaterLayer = true;
		Config.GridResolution = 192;
		Config.SimExtentCm = FVector2D(3600.0f, 3600.0f);
		Config.PressureIterations = 12;
		Config.VelocityDamping = 0.02f;
		Config.DensityDissipation = 0.35f;
		Config.AdvectionSpeedScale = 1.0f;
		Config.TerrainSlopeGravity = 0.0f;
		Config.VorticityStrength = 0.3f;
		Config.WhitewaterFromVorticity = 0.15f;
		Config.ActorBrushRippleImpulseCm = 2.5f;
		Config.ActorBrushDensityStrength = 0.6f;
	}

	struct FTDFluidFirePresetValues
	{
		ETDFluidSolverBackend Backend = ETDFluidSolverBackend::Cpu;
		int32 Resolution = 128;
		int32 CpuFallbackResolution = 0;
		FVector2D ExtentCm = FVector2D(400.0, 400.0);
		float VelocityDamping = 0.1f;
		int32 PressureIterations = 8;
		float DensityDissipation = 0.5f;
		float VorticityStrength = 2.0f;
		float BuoyancyCmPerSec2 = 0.0f;
		bool bUsesHeat = false;
		float HeatCoolingPerSec = 2.0f;
		float HeatBuoyancyCmPerSec2 = 0.0f;
		float OutflowBandRatio = 0.0f;
		float OutflowRatePerSec = 0.0f;
		float FlowNoiseAccelerationCmPerSec2 = 0.0f;
		float FlowNoiseScaleCm = 200.0f;
		float FlowNoiseSpeed = 0.12f;
		int32 MaxPointsPerTick = 128;
	};

	void ApplyFirePresetValues(FTDFluidSimulationConfig& Config, const FTDFluidFirePresetValues& Values)
	{
		Config.SimMode = ETDFluidSimMode::FluidSim;
		Config.SolverBackend = Values.Backend;
		Config.GridResolution = Values.Resolution;
		Config.CpuFallbackResolution = Values.CpuFallbackResolution;
		Config.SimExtentCm = Values.ExtentCm;
		Config.VelocityDamping = Values.VelocityDamping;
		Config.PressureIterations = Values.PressureIterations;
		Config.DensityDissipation = Values.DensityDissipation;
		Config.VorticityStrength = Values.VorticityStrength;
		Config.TerrainSlopeGravity = 0.0f;
		Config.AdvectionSpeedScale = 1.0f;
		Config.BorderFadeRatio = 0.1f;
		Config.MaxPointsPerTick = Values.MaxPointsPerTick;
		Config.BuoyancyCmPerSec2 = Values.BuoyancyCmPerSec2;
		Config.bEnableHeatChannel = Values.bUsesHeat;
		Config.HeatCoolingPerSec = Values.HeatCoolingPerSec;
		Config.HeatBuoyancyCmPerSec2 = Values.HeatBuoyancyCmPerSec2;
		Config.OutflowBandRatio = Values.OutflowBandRatio;
		Config.OutflowRatePerSec = Values.OutflowRatePerSec;
		Config.FlowNoiseAccelerationCmPerSec2 = Values.FlowNoiseAccelerationCmPerSec2;
		Config.FlowNoiseScaleCm = Values.FlowNoiseScaleCm;
		Config.FlowNoiseSpeed = Values.FlowNoiseSpeed;
	}

	FTDFluidFirePresetValues MakeExplosionValues()
	{
		FTDFluidFirePresetValues Values;
		Values.Backend = ETDFluidSolverBackend::Gpu;
		Values.Resolution = 256;
		Values.CpuFallbackResolution = 128;
		Values.ExtentCm = FVector2D(900.0, 900.0);
		Values.VelocityDamping = 0.12f;
		Values.PressureIterations = 12;
		Values.DensityDissipation = 0.9f;
		Values.VorticityStrength = 4.0f;
		Values.bUsesHeat = true;
		Values.HeatCoolingPerSec = 0.7f;
		Values.HeatBuoyancyCmPerSec2 = 2000.0f;
		Values.OutflowBandRatio = 0.12f;
		Values.OutflowRatePerSec = 4.0f;
		Values.FlowNoiseAccelerationCmPerSec2 = 300.0f;
		Values.FlowNoiseScaleCm = 120.0f;
		Values.FlowNoiseSpeed = 1.0f;
		Values.MaxPointsPerTick = 256;
		return Values;
	}

	void ApplyVortexValues(FTDFluidSimulationConfig& Config, float TangentialAccelerationCmPerSec2, float RadialDriftCmPerSec)
	{
		FTDFluidFirePresetValues Values;
		Values.ExtentCm = FVector2D(600.0, 600.0);
		Values.VelocityDamping = 0.1f;
		Values.DensityDissipation = 0.05f;
		Values.VorticityStrength = 1.0f;
		ApplyFirePresetValues(Config, Values);
		Config.VortexTangentialAccelerationCmPerSec2 = TangentialAccelerationCmPerSec2;
		Config.VortexRadialDriftCmPerSec = RadialDriftCmPerSec;
		Config.VortexRadiusRatio = 0.35f;
	}
}

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

	case ETDFluidPresetKind::River:
		ApplyWaterLayerBaseConfig(Config);
		Config.RapidsWhitewaterPerSec = 2.0f;
		Config.WhitewaterFromVorticity = 0.3f;
		Config.RippleWaveSpeedCmPerSec = 120.0f;
		Config.FlowNoiseAccelerationCmPerSec2 = 150.0f;
		Config.BankWetHeightCm = 10.0f;
		Config.SwashHeightCm = 0.0f;
		break;

	case ETDFluidPresetKind::Sea:
		ApplyWaterLayerBaseConfig(Config);
		Config.RippleWaveSpeedCmPerSec = 100.0f;
		Config.SwashHeightCm = 25.0f;
		Config.WetnessDryingSeconds = 8.0f;
		Config.BankWetHeightCm = 0.0f;
		break;

	case ETDFluidPresetKind::Torch:
	{
		FTDFluidFirePresetValues Values;
		Values.Resolution = 96;
		Values.ExtentCm = FVector2D(110.0, 170.0);
		Values.VelocityDamping = 0.2f;
		Values.PressureIterations = 8;
		Values.DensityDissipation = 1.0f;
		Values.VorticityStrength = 3.0f;
		Values.bUsesHeat = true;
		Values.HeatCoolingPerSec = 11.0f;
		Values.HeatBuoyancyCmPerSec2 = 600.0f;
		Values.OutflowBandRatio = 0.15f;
		Values.OutflowRatePerSec = 6.0f;
		Values.FlowNoiseAccelerationCmPerSec2 = 400.0f;
		Values.FlowNoiseScaleCm = 14.0f;
		Values.FlowNoiseSpeed = 1.5f;
		ApplyFirePresetValues(Config, Values);
		break;
	}
	case ETDFluidPresetKind::Campfire:
	{
		FTDFluidFirePresetValues Values;
		Values.Resolution = 128;
		Values.ExtentCm = FVector2D(240.0, 340.0);
		Values.VelocityDamping = 0.15f;
		Values.PressureIterations = 10;
		Values.DensityDissipation = 1.0f;
		Values.VorticityStrength = 1.5f;
		Values.bUsesHeat = true;
		Values.HeatCoolingPerSec = 0.6f;
		Values.HeatBuoyancyCmPerSec2 = 1600.0f;
		Values.OutflowBandRatio = 0.15f;
		Values.OutflowRatePerSec = 6.0f;
		Values.FlowNoiseAccelerationCmPerSec2 = 300.0f;
		Values.FlowNoiseScaleCm = 35.0f;
		Values.FlowNoiseSpeed = 1.5f;
		ApplyFirePresetValues(Config, Values);
		break;
	}
	case ETDFluidPresetKind::CampfireSmoke:
	{
		FTDFluidFirePresetValues Values;
		Values.Resolution = 96;
		Values.ExtentCm = FVector2D(400.0, 560.0);
		Values.VelocityDamping = 0.08f;
		Values.PressureIterations = 8;
		Values.DensityDissipation = 0.2f;
		Values.VorticityStrength = 2.0f;
		Values.BuoyancyCmPerSec2 = 160.0f;
		Values.OutflowBandRatio = 0.2f;
		Values.OutflowRatePerSec = 3.0f;
		Values.FlowNoiseAccelerationCmPerSec2 = 300.0f;
		Values.FlowNoiseScaleCm = 60.0f;
		Values.FlowNoiseSpeed = 0.5f;
		ApplyFirePresetValues(Config, Values);
		break;
	}
	case ETDFluidPresetKind::Explosion:
	case ETDFluidPresetKind::PlasmaExplosion:
		ApplyFirePresetValues(Config, MakeExplosionValues());
		break;
	case ETDFluidPresetKind::ExplosionSmoke:
	{
		FTDFluidFirePresetValues Values;
		Values.Backend = ETDFluidSolverBackend::Gpu;
		Values.Resolution = 256;
		Values.CpuFallbackResolution = 112;
		Values.ExtentCm = FVector2D(1200.0, 1200.0);
		Values.VelocityDamping = 0.08f;
		Values.PressureIterations = 10;
		Values.DensityDissipation = 0.12f;
		Values.VorticityStrength = 2.0f;
		Values.bUsesHeat = true;
		Values.HeatCoolingPerSec = 0.6f;
		Values.HeatBuoyancyCmPerSec2 = 150.0f;
		Values.OutflowBandRatio = 0.2f;
		Values.OutflowRatePerSec = 2.0f;
		Values.FlowNoiseAccelerationCmPerSec2 = 250.0f;
		Values.FlowNoiseScaleCm = 120.0f;
		Values.FlowNoiseSpeed = 0.5f;
		Values.MaxPointsPerTick = 256;
		ApplyFirePresetValues(Config, Values);
		break;
	}
	case ETDFluidPresetKind::ExplosionGroundDust:
	{
		FTDFluidFirePresetValues Values;
		Values.Resolution = 128;
		Values.ExtentCm = FVector2D(1200.0, 1200.0);
		Values.VelocityDamping = 0.1f;
		Values.PressureIterations = 8;
		Values.DensityDissipation = 0.2f;
		Values.VorticityStrength = 1.5f;
		Values.OutflowBandRatio = 0.25f;
		Values.OutflowRatePerSec = 2.0f;
		Values.FlowNoiseAccelerationCmPerSec2 = 200.0f;
		Values.FlowNoiseScaleCm = 150.0f;
		Values.FlowNoiseSpeed = 0.4f;
		Values.MaxPointsPerTick = 256;
		ApplyFirePresetValues(Config, Values);
		break;
	}
	case ETDFluidPresetKind::Candle:
	{
		FTDFluidFirePresetValues Values;
		Values.Resolution = 64;
		Values.ExtentCm = FVector2D(40.0, 70.0);
		Values.VelocityDamping = 0.2f;
		Values.PressureIterations = 6;
		Values.DensityDissipation = 1.2f;
		Values.VorticityStrength = 1.5f;
		Values.bUsesHeat = true;
		Values.HeatCoolingPerSec = 12.0f;
		Values.HeatBuoyancyCmPerSec2 = 400.0f;
		Values.OutflowBandRatio = 0.15f;
		Values.OutflowRatePerSec = 6.0f;
		Values.FlowNoiseAccelerationCmPerSec2 = 150.0f;
		Values.FlowNoiseScaleCm = 6.0f;
		Values.FlowNoiseSpeed = 1.2f;
		ApplyFirePresetValues(Config, Values);
		break;
	}
	case ETDFluidPresetKind::Fireball:
	{
		FTDFluidFirePresetValues Values;
		Values.Resolution = 128;
		Values.ExtentCm = FVector2D(400.0, 400.0);
		Values.VelocityDamping = 0.12f;
		Values.PressureIterations = 8;
		Values.DensityDissipation = 0.8f;
		Values.VorticityStrength = 3.0f;
		Values.bUsesHeat = true;
		Values.HeatCoolingPerSec = 1.2f;
		Values.HeatBuoyancyCmPerSec2 = 600.0f;
		Values.OutflowBandRatio = 0.12f;
		Values.OutflowRatePerSec = 4.0f;
		Values.FlowNoiseAccelerationCmPerSec2 = 600.0f;
		Values.FlowNoiseScaleCm = 40.0f;
		Values.FlowNoiseSpeed = 1.0f;
		ApplyFirePresetValues(Config, Values);
		break;
	}
	case ETDFluidPresetKind::SteamPuff:
	{
		FTDFluidFirePresetValues Values;
		Values.Resolution = 128;
		Values.ExtentCm = FVector2D(500.0, 500.0);
		Values.VelocityDamping = 0.1f;
		Values.PressureIterations = 8;
		Values.DensityDissipation = 0.6f;
		Values.VorticityStrength = 2.0f;
		Values.BuoyancyCmPerSec2 = 250.0f;
		Values.OutflowBandRatio = 0.2f;
		Values.OutflowRatePerSec = 3.0f;
		Values.FlowNoiseAccelerationCmPerSec2 = 300.0f;
		Values.FlowNoiseScaleCm = 60.0f;
		Values.FlowNoiseSpeed = 0.6f;
		ApplyFirePresetValues(Config, Values);
		break;
	}
	case ETDFluidPresetKind::Vortex:
		ApplyVortexValues(Config, 600.0f, 60.0f);
		break;
	case ETDFluidPresetKind::InvertedVortex:
		ApplyVortexValues(Config, -600.0f, -60.0f);
		break;
	case ETDFluidPresetKind::PortalVortex:
		ApplyVortexValues(Config, 900.0f, 250.0f);
		break;
	case ETDFluidPresetKind::Custom:
	default:
		break;
	}

	return Config;
}

bool UTDFluidPresetAsset::IsFireFamilyPresetKind(ETDFluidPresetKind Kind)
{
	return static_cast<uint8>(Kind) >= static_cast<uint8>(ETDFluidPresetKind::Torch);
}

FTDFluidSourceEmitterSettings UTDFluidPresetAsset::BuildSourceEmitterForPresetKind(ETDFluidPresetKind Kind)
{
	FTDFluidSourceEmitterSettings Emitter;
	switch (Kind)
	{
	case ETDFluidPresetKind::Torch:
		Emitter.bIsEnabled = true;
		Emitter.RadiusCm = 10.0f;
		Emitter.DensityStrength = 0.7f;
		Emitter.EmitSpeedCmPerSec = 300.0f;
		Emitter.MotionInertiaScale = 0.2f;
		Emitter.SubstepCount = 3;
		Emitter.CoreRadiusCm = 9.0f;
		Emitter.CoreDensityStrength = 0.85f;
		Emitter.CoreHeatStrength = 1.0f;
		Emitter.HeatStrength = 0.9f;
		break;
	case ETDFluidPresetKind::Campfire:
		Emitter.bIsEnabled = true;
		Emitter.SourceStampCount = 5;
		Emitter.SourceSpreadCm = 60.0f;
		Emitter.RadiusCm = 26.0f;
		Emitter.DensityStrength = 0.8f;
		Emitter.EmitSpeedCmPerSec = 300.0f;
		Emitter.MotionInertiaScale = 0.3f;
		Emitter.CoreRadiusCm = 30.0f;
		Emitter.CoreHeatStrength = 1.0f;
		Emitter.HeatStrength = 0.9f;
		break;
	case ETDFluidPresetKind::Candle:
		Emitter.bIsEnabled = true;
		Emitter.RadiusCm = 3.0f;
		Emitter.EmitSpeedCmPerSec = 120.0f;
		Emitter.SubstepCount = 2;
		Emitter.CoreRadiusCm = 3.0f;
		Emitter.HeatStrength = 0.9f;
		break;
	case ETDFluidPresetKind::Fireball:
		Emitter.bIsEnabled = true;
		Emitter.RadiusCm = 40.0f;
		Emitter.EmitSpeedCmPerSec = 200.0f;
		Emitter.CoreRadiusCm = 50.0f;
		Emitter.HeatStrength = 1.0f;
		break;
	default:
		break;
	}
	return Emitter;
}

FTDFluidFragmentTrailSettings UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(ETDFluidPresetKind Kind)
{
	FTDFluidFragmentTrailSettings Trail;
	if (Kind != ETDFluidPresetKind::Explosion && Kind != ETDFluidPresetKind::PlasmaExplosion)
	{
		return Trail;
	}

	Trail.BurstCoreRadiusCm = 224.0f;
	Trail.BurstRingRadiusCm = 90.0f;
	Trail.BurstRingStampCount = 0;
	Trail.BurstRingSpeedCmPerSec = 450.0f;
	Trail.BurstSustainSeconds = 0.14f;
	Trail.bSustainsBurstHeat = true;
	Trail.bShouldTrackWithWorldClock = true;
	Trail.BurstCenterHeightOffsetCm = 160.0f;
	Trail.FireHeatStrength = 1.0f;
	Trail.SmokeHeatStrength = 0.6f;
	Trail.TrackingDurationSeconds = 8.0f;
	Trail.RestingHeatSeconds = 8.0f;
	Trail.RestingHeatStrength = 1.0f;
	Trail.RestingHeatRadiusCm = 16.0f;
	Trail.RestingDensityStrength = 0.4f;
	Trail.MaxFragmentsPerStep = 64;
	Trail.FireStampRadiusCm = 20.0f;
	Trail.SmokeStampRadiusCm = 40.0f;
	Trail.SmokeDensityStrength = 0.75f;
	return Trail;
}

FTDFluidVolumeRenderSettings UTDFluidPresetAsset::BuildVolumeRenderForPresetKind(ETDFluidPresetKind Kind)
{
	FTDFluidVolumeRenderSettings VolumeRender;
	switch (Kind)
	{
	case ETDFluidPresetKind::CampfireSmoke:
		VolumeRender.bEnableTransmittance = true;
		VolumeRender.DensityScale = 0.035f;
		VolumeRender.SoftFalloffCm = 28.0f;
		break;
	case ETDFluidPresetKind::ExplosionSmoke:
		VolumeRender.bEnableTransmittance = true;
		VolumeRender.DensityScale = 0.03f;
		VolumeRender.SoftFalloffCm = 40.0f;
		VolumeRender.HeatEmissionIntensity = 6.0f;
		VolumeRender.HeatEmissionColor = FLinearColor(1.0f, 0.06f, 0.01f, 1.0f);
		VolumeRender.HeatEmissionExponent = 3.0f;
		break;
	case ETDFluidPresetKind::ExplosionGroundDust:
		VolumeRender.bEnableTransmittance = true;
		VolumeRender.DensityScale = 0.025f;
		VolumeRender.SoftFalloffCm = 30.0f;
		break;
	case ETDFluidPresetKind::SteamPuff:
		VolumeRender.bEnableTransmittance = true;
		VolumeRender.DensityScale = 0.02f;
		break;
	default:
		break;
	}
	return VolumeRender;
}

FTDFluidFlameCardLayerSettings UTDFluidPresetAsset::BuildFlameCardLayersForPresetKind(ETDFluidPresetKind Kind)
{
	FTDFluidFlameCardLayerSettings CardLayers;
	switch (Kind)
	{
	case ETDFluidPresetKind::Torch:
		CardLayers.ExtraCardCount = 2;
		CardLayers.CardDepthSpacingCm = 6.0f;
		break;
	case ETDFluidPresetKind::Campfire:
		CardLayers.ExtraCardCount = 1;
		CardLayers.CardDepthSpacingCm = 14.0f;
		break;
	case ETDFluidPresetKind::Explosion:
	case ETDFluidPresetKind::PlasmaExplosion:
		CardLayers.ExtraCardCount = 1;
		CardLayers.CardDepthSpacingCm = 30.0f;
		break;
	default:
		break;
	}
	return CardLayers;
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
	case ETDFluidPresetKind::Torch:
	case ETDFluidPresetKind::Campfire:
	case ETDFluidPresetKind::Explosion:
	case ETDFluidPresetKind::Candle:
	case ETDFluidPresetKind::Fireball:
	case ETDFluidPresetKind::PlasmaExplosion:
		Preset->DefaultChannel = ETDFluidBrushChannel::ChannelB_Fire;
		break;
	case ETDFluidPresetKind::CampfireSmoke:
	case ETDFluidPresetKind::ExplosionSmoke:
	case ETDFluidPresetKind::ExplosionGroundDust:
	case ETDFluidPresetKind::SteamPuff:
	case ETDFluidPresetKind::Vortex:
	case ETDFluidPresetKind::InvertedVortex:
	case ETDFluidPresetKind::PortalVortex:
		Preset->DefaultChannel = ETDFluidBrushChannel::ChannelR_Mud;
		break;
	case ETDFluidPresetKind::Water:
	case ETDFluidPresetKind::Custom:
	default:
		Preset->DefaultChannel = ETDFluidBrushChannel::ChannelA_Water;
		break;
	}
	Preset->VolumeRender = BuildVolumeRenderForPresetKind(Kind);

	return Preset;
}
