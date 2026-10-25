#pragma once

#include "FluidFX/TDFluidTypes.h"

namespace TDFluidGoldenPresetSnapshots
{
	inline FTDFluidSimulationConfig MakeSimulationConfig(ETDFluidPresetKind Kind)
	{
		FTDFluidSimulationConfig Settings;
		switch (Kind)
		{
		case ETDFluidPresetKind::Water:
			Settings.AdvectionSpeedScale = 0x1.333334p+0f;
			Settings.DensityDissipation = 0x1.99999ap-3f;
			Settings.PressureIterations = 8;
			Settings.TerrainSlopeGravity = 0x1.2cp+9f;
			Settings.VelocityDamping = 0x1.47ae14p-6f;
			Settings.VorticityStrength = 0x1.99999ap-1f;
			break;
		case ETDFluidPresetKind::Mud:
			Settings.AdvectionSpeedScale = 0x1.666666p-2f;
			Settings.DensityDissipation = 0x1.47ae14p-7f;
			Settings.SimExtentCm = FVector2D(0x1.9p+11, 0x1.9p+11);
			Settings.TerrainSlopeGravity = 0x1.ep+6f;
			Settings.VelocityDamping = 0x1.4cccccp-1f;
			Settings.VorticityStrength = 0x1.333334p-3f;
			break;
		case ETDFluidPresetKind::Blood:
			Settings.AdvectionSpeedScale = 0x1.19999ap-1f;
			Settings.BorderFadeRatio = 0x1.eb851ep-4f;
			Settings.DensityDissipation = 0x1.47ae14p-7f;
			Settings.MaxPointsPerTick = 48;
			Settings.SimExtentCm = FVector2D(0x1.2cp+11, 0x1.2cp+11);
			Settings.TerrainSlopeGravity = 0x1.c2p+8f;
			Settings.VelocityDamping = 0x1.ccccccp-2f;
			Settings.VorticityStrength = 0x1.333334p-2f;
			break;
		case ETDFluidPresetKind::Fire:
			Settings.AdvectionSpeedScale = 0x1.59999ap+0f;
			Settings.BorderFadeRatio = 0x1.333334p-3f;
			Settings.DensityDissipation = 0x1.ccccccp-2f;
			Settings.SimExtentCm = FVector2D(0x1.5ep+11, 0x1.5ep+11);
			Settings.TerrainSlopeGravity = 0x1.9p+7f;
			Settings.VelocityDamping = 0x1.333334p-3f;
			Settings.VorticityStrength = 0x1.4p+1f;
			break;
		case ETDFluidPresetKind::FootprintPainter:
			Settings.AdvectionSpeedScale = 0x0.0p+0f;
			Settings.DensityDissipation = 0x1.47ae14p-7f;
			Settings.PressureIterations = 1;
			Settings.SimMode = ETDFluidSimMode::SimplePainter;
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.0p+0f;
			Settings.VorticityStrength = 0x0.0p+0f;
			break;
		case ETDFluidPresetKind::Smoke:
			Settings.ActorBrushDensityStrength = -0x1.0p+0f;
			Settings.ActorBrushPunctureCmPerSec = 0x1.7cp+8f;
			Settings.AmbientDensity = 0x1.99999ap-1f;
			Settings.AmbientNoiseAmount = 0x1.ccccccp-2f;
			Settings.AmbientRefillPerSec = 0x1.0p-1f;
			Settings.DensityDissipation = 0x0.0p+0f;
			Settings.FlowNoiseAccelerationCmPerSec2 = 0x1.18p+6f;
			Settings.FlowNoiseScaleCm = 0x1.b8p+7f;
			Settings.GridResolution = 256;
			Settings.SimExtentCm = FVector2D(0x1.5ep+9, 0x1.5ep+9);
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.47ae14p-4f;
			Settings.VorticityStrength = 0x1.333334p+0f;
			break;
		case ETDFluidPresetKind::River:
			Settings.ActorBrushDensityStrength = 0x1.333334p-1f;
			Settings.ActorBrushRippleImpulseCm = 0x1.4p+1f;
			Settings.BankWetHeightCm = 0x1.4p+3f;
			Settings.DensityDissipation = 0x1.666666p-2f;
			Settings.FlowNoiseAccelerationCmPerSec2 = 0x1.2cp+7f;
			Settings.GridResolution = 192;
			Settings.PressureIterations = 12;
			Settings.RapidsWhitewaterPerSec = 0x1.0p+1f;
			Settings.RippleWaveSpeedCmPerSec = 0x1.ep+6f;
			Settings.SimExtentCm = FVector2D(0x1.c2p+11, 0x1.c2p+11);
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.47ae14p-6f;
			Settings.VorticityStrength = 0x1.333334p-2f;
			Settings.WhitewaterFromVorticity = 0x1.333334p-2f;
			Settings.bEnableWaterLayer = true;
			Settings.bEnableWaterWetness = true;
			break;
		case ETDFluidPresetKind::Sea:
			Settings.ActorBrushDensityStrength = 0x1.333334p-1f;
			Settings.ActorBrushRippleImpulseCm = 0x1.4p+1f;
			Settings.DensityDissipation = 0x1.666666p-2f;
			Settings.GridResolution = 192;
			Settings.PressureIterations = 12;
			Settings.RippleWaveSpeedCmPerSec = 0x1.9p+6f;
			Settings.SimExtentCm = FVector2D(0x1.c2p+11, 0x1.c2p+11);
			Settings.SwashHeightCm = 0x1.9p+4f;
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.47ae14p-6f;
			Settings.VorticityStrength = 0x1.333334p-2f;
			Settings.WhitewaterFromVorticity = 0x1.333334p-3f;
			Settings.bEnableWaterLayer = true;
			Settings.bEnableWaterWetness = true;
			Settings.bUseWhitewaterDissipation = true;
			break;
		case ETDFluidPresetKind::Torch:
			Settings.DensityDissipation = 0x1.0p+0f;
			Settings.FlowNoiseAccelerationCmPerSec2 = 0x1.9p+8f;
			Settings.FlowNoiseScaleCm = 0x1.cp+3f;
			Settings.FlowNoiseSpeed = 0x1.8p+0f;
			Settings.GridResolution = 96;
			Settings.HeatBuoyancyCmPerSec2 = 0x1.2cp+9f;
			Settings.HeatCoolingPerSec = 0x1.6p+3f;
			Settings.MaxPointsPerTick = 128;
			Settings.OutflowBandRatio = 0x1.333334p-3f;
			Settings.OutflowRatePerSec = 0x1.8p+2f;
			Settings.PressureIterations = 8;
			Settings.SimExtentCm = FVector2D(0x1.b8p+6, 0x1.54p+7);
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.99999ap-3f;
			Settings.VorticityStrength = 0x1.8p+1f;
			Settings.bEnableHeatChannel = true;
			break;
		case ETDFluidPresetKind::Campfire:
			Settings.DensityDissipation = 0x1.0p+0f;
			Settings.FlowNoiseAccelerationCmPerSec2 = 0x1.2cp+8f;
			Settings.FlowNoiseScaleCm = 0x1.18p+5f;
			Settings.FlowNoiseSpeed = 0x1.8p+0f;
			Settings.GridResolution = 128;
			Settings.HeatBuoyancyCmPerSec2 = 0x1.9p+10f;
			Settings.HeatCoolingPerSec = 0x1.333334p-1f;
			Settings.MaxPointsPerTick = 128;
			Settings.OutflowBandRatio = 0x1.333334p-3f;
			Settings.OutflowRatePerSec = 0x1.8p+2f;
			Settings.PressureIterations = 10;
			Settings.SimExtentCm = FVector2D(0x1.ep+7, 0x1.54p+8);
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.333334p-3f;
			Settings.VorticityStrength = 0x1.8p+0f;
			Settings.bEnableHeatChannel = true;
			break;
		case ETDFluidPresetKind::CampfireSmoke:
			Settings.BuoyancyCmPerSec2 = 0x1.4p+7f;
			Settings.DensityDissipation = 0x1.99999ap-3f;
			Settings.FlowNoiseAccelerationCmPerSec2 = 0x1.2cp+8f;
			Settings.FlowNoiseScaleCm = 0x1.ep+5f;
			Settings.FlowNoiseSpeed = 0x1.0p-1f;
			Settings.GridResolution = 96;
			Settings.MaxPointsPerTick = 128;
			Settings.OutflowBandRatio = 0x1.99999ap-3f;
			Settings.OutflowRatePerSec = 0x1.8p+1f;
			Settings.PressureIterations = 8;
			Settings.SimExtentCm = FVector2D(0x1.9p+8, 0x1.18p+9);
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.47ae14p-4f;
			Settings.VorticityStrength = 0x1.0p+1f;
			break;
		case ETDFluidPresetKind::Explosion:
			Settings.CpuFallbackResolution = 128;
			Settings.DensityDissipation = 0x1.ccccccp-1f;
			Settings.FlowNoiseAccelerationCmPerSec2 = 0x1.2cp+8f;
			Settings.FlowNoiseScaleCm = 0x1.ep+6f;
			Settings.FlowNoiseSpeed = 0x1.0p+0f;
			Settings.GridResolution = 256;
			Settings.HeatBuoyancyCmPerSec2 = 0x1.f4p+10f;
			Settings.HeatCoolingPerSec = 0x1.666666p-1f;
			Settings.MaxPointsPerTick = 256;
			Settings.OutflowBandRatio = 0x1.eb851ep-4f;
			Settings.OutflowRatePerSec = 0x1.0p+2f;
			Settings.PressureIterations = 12;
			Settings.SimExtentCm = FVector2D(0x1.c2p+9, 0x1.c2p+9);
			Settings.SolverBackend = ETDFluidSolverBackend::Gpu;
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.eb851ep-4f;
			Settings.VorticityStrength = 0x1.0p+2f;
			Settings.bEnableHeatChannel = true;
			break;
		case ETDFluidPresetKind::ExplosionSmoke:
			Settings.CpuFallbackResolution = 112;
			Settings.DensityDissipation = 0x1.eb851ep-4f;
			Settings.FlowNoiseAccelerationCmPerSec2 = 0x1.f4p+7f;
			Settings.FlowNoiseScaleCm = 0x1.ep+6f;
			Settings.FlowNoiseSpeed = 0x1.0p-1f;
			Settings.GridResolution = 256;
			Settings.HeatBuoyancyCmPerSec2 = 0x1.2cp+7f;
			Settings.HeatCoolingPerSec = 0x1.333334p-1f;
			Settings.MaxPointsPerTick = 256;
			Settings.OutflowBandRatio = 0x1.99999ap-3f;
			Settings.OutflowRatePerSec = 0x1.0p+1f;
			Settings.PressureIterations = 10;
			Settings.SimExtentCm = FVector2D(0x1.2cp+10, 0x1.2cp+10);
			Settings.SolverBackend = ETDFluidSolverBackend::Gpu;
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.47ae14p-4f;
			Settings.VorticityStrength = 0x1.0p+1f;
			Settings.bEnableHeatChannel = true;
			break;
		case ETDFluidPresetKind::ExplosionGroundDust:
			Settings.DensityDissipation = 0x1.99999ap-3f;
			Settings.FlowNoiseAccelerationCmPerSec2 = 0x1.9p+7f;
			Settings.FlowNoiseScaleCm = 0x1.2cp+7f;
			Settings.FlowNoiseSpeed = 0x1.99999ap-2f;
			Settings.GridResolution = 128;
			Settings.MaxPointsPerTick = 256;
			Settings.OutflowBandRatio = 0x1.0p-2f;
			Settings.OutflowRatePerSec = 0x1.0p+1f;
			Settings.PressureIterations = 8;
			Settings.SimExtentCm = FVector2D(0x1.2cp+10, 0x1.2cp+10);
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.99999ap-4f;
			Settings.VorticityStrength = 0x1.8p+0f;
			break;
		case ETDFluidPresetKind::Candle:
			Settings.DensityDissipation = 0x1.333334p+0f;
			Settings.FlowNoiseAccelerationCmPerSec2 = 0x1.2cp+7f;
			Settings.FlowNoiseScaleCm = 0x1.8p+2f;
			Settings.FlowNoiseSpeed = 0x1.333334p+0f;
			Settings.HeatBuoyancyCmPerSec2 = 0x1.9p+8f;
			Settings.HeatCoolingPerSec = 0x1.8p+3f;
			Settings.MaxPointsPerTick = 128;
			Settings.OutflowBandRatio = 0x1.333334p-3f;
			Settings.OutflowRatePerSec = 0x1.8p+2f;
			Settings.SimExtentCm = FVector2D(0x1.4p+5, 0x1.18p+6);
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.99999ap-3f;
			Settings.VorticityStrength = 0x1.8p+0f;
			Settings.bEnableHeatChannel = true;
			break;
		case ETDFluidPresetKind::Fireball:
			Settings.DensityDissipation = 0x1.99999ap-1f;
			Settings.FlowNoiseAccelerationCmPerSec2 = 0x1.2cp+9f;
			Settings.FlowNoiseScaleCm = 0x1.4p+5f;
			Settings.FlowNoiseSpeed = 0x1.0p+0f;
			Settings.GridResolution = 128;
			Settings.HeatBuoyancyCmPerSec2 = 0x1.2cp+9f;
			Settings.HeatCoolingPerSec = 0x1.333334p+0f;
			Settings.MaxPointsPerTick = 128;
			Settings.OutflowBandRatio = 0x1.eb851ep-4f;
			Settings.OutflowRatePerSec = 0x1.0p+2f;
			Settings.PressureIterations = 8;
			Settings.SimExtentCm = FVector2D(0x1.9p+8, 0x1.9p+8);
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.eb851ep-4f;
			Settings.VorticityStrength = 0x1.8p+1f;
			Settings.bEnableHeatChannel = true;
			break;
		case ETDFluidPresetKind::PlasmaExplosion:
			Settings.CpuFallbackResolution = 128;
			Settings.DensityDissipation = 0x1.ccccccp-1f;
			Settings.FlowNoiseAccelerationCmPerSec2 = 0x1.2cp+8f;
			Settings.FlowNoiseScaleCm = 0x1.ep+6f;
			Settings.FlowNoiseSpeed = 0x1.0p+0f;
			Settings.GridResolution = 256;
			Settings.HeatBuoyancyCmPerSec2 = 0x1.f4p+10f;
			Settings.HeatCoolingPerSec = 0x1.666666p-1f;
			Settings.MaxPointsPerTick = 256;
			Settings.OutflowBandRatio = 0x1.eb851ep-4f;
			Settings.OutflowRatePerSec = 0x1.0p+2f;
			Settings.PressureIterations = 12;
			Settings.SimExtentCm = FVector2D(0x1.c2p+9, 0x1.c2p+9);
			Settings.SolverBackend = ETDFluidSolverBackend::Gpu;
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.eb851ep-4f;
			Settings.VorticityStrength = 0x1.0p+2f;
			Settings.bEnableHeatChannel = true;
			break;
		case ETDFluidPresetKind::SteamPuff:
			Settings.BuoyancyCmPerSec2 = 0x1.f4p+7f;
			Settings.DensityDissipation = 0x1.333334p-1f;
			Settings.FlowNoiseAccelerationCmPerSec2 = 0x1.2cp+8f;
			Settings.FlowNoiseScaleCm = 0x1.ep+5f;
			Settings.FlowNoiseSpeed = 0x1.333334p-1f;
			Settings.GridResolution = 128;
			Settings.MaxPointsPerTick = 128;
			Settings.OutflowBandRatio = 0x1.99999ap-3f;
			Settings.OutflowRatePerSec = 0x1.8p+1f;
			Settings.PressureIterations = 8;
			Settings.SimExtentCm = FVector2D(0x1.f4p+8, 0x1.f4p+8);
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.99999ap-4f;
			Settings.VorticityStrength = 0x1.0p+1f;
			break;
		case ETDFluidPresetKind::Vortex:
			Settings.DensityDissipation = 0x1.99999ap-5f;
			Settings.GridResolution = 128;
			Settings.MaxPointsPerTick = 128;
			Settings.PressureIterations = 8;
			Settings.SimExtentCm = FVector2D(0x1.2cp+9, 0x1.2cp+9);
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.99999ap-4f;
			Settings.VortexRadialDriftCmPerSec = 0x1.ep+5f;
			Settings.VortexTangentialAccelerationCmPerSec2 = 0x1.2cp+9f;
			break;
		case ETDFluidPresetKind::InvertedVortex:
			Settings.DensityDissipation = 0x1.99999ap-5f;
			Settings.GridResolution = 128;
			Settings.MaxPointsPerTick = 128;
			Settings.PressureIterations = 8;
			Settings.SimExtentCm = FVector2D(0x1.2cp+9, 0x1.2cp+9);
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.99999ap-4f;
			Settings.VortexRadialDriftCmPerSec = -0x1.ep+5f;
			Settings.VortexTangentialAccelerationCmPerSec2 = -0x1.2cp+9f;
			break;
		case ETDFluidPresetKind::PortalVortex:
			Settings.DensityDissipation = 0x1.99999ap-5f;
			Settings.GridResolution = 128;
			Settings.MaxPointsPerTick = 128;
			Settings.PressureIterations = 8;
			Settings.SimExtentCm = FVector2D(0x1.2cp+9, 0x1.2cp+9);
			Settings.TerrainSlopeGravity = 0x0.0p+0f;
			Settings.VelocityDamping = 0x1.99999ap-4f;
			Settings.VortexRadialDriftCmPerSec = 0x1.f4p+7f;
			Settings.VortexTangentialAccelerationCmPerSec2 = 0x1.c2p+9f;
			break;
		default:
			break;
		}
		return Settings;
	}
}
