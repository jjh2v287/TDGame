#pragma once

#include "CoreMinimal.h"

class FTextureRenderTargetResource;

namespace TDFluidGpuSolverConstants
{
	inline constexpr int32 MinimumResolution = 16;
	inline constexpr int32 MaximumResolution = 2048;
	inline constexpr int32 CpuMaximumResolution = 256;
	inline constexpr int32 MinimumPressureIterations = 1;
	inline constexpr int32 MaximumPressureIterations = 32;
	inline constexpr int32 MultigridMinimumResolution = 64;
	inline constexpr int32 MultigridAutoMinimumResolution = 512;
	inline constexpr int32 MultigridCoarsestResolution = 32;
	inline constexpr int32 MinimumMultigridCycles = 1;
	inline constexpr int32 MaximumMultigridCycles = 4;
	inline constexpr int32 DefaultMultigridCycles = 1;
	inline constexpr float MultigridPressureLimit = 1.0e6f;
	inline constexpr int32 DefaultMultigridWarmStartDecayFrames = 32;
	inline constexpr int32 MaximumStampsPerTick = 16000;
	inline constexpr int32 MaximumStampsPerBin = 256;
	inline constexpr int32 StampBinsPerAxis = 16;
	inline constexpr int32 StampBinCount = StampBinsPerAxis * StampBinsPerAxis;
	inline constexpr int32 ReadbackTileResolution = 64;
	inline constexpr int32 ReadbackTilesPerAtlasRow = 8;
	inline constexpr int32 ReadbackRingSize = 3;
	inline constexpr float MaximumStepDeltaTimeSeconds = 0.1f;
	inline constexpr float BrushVelocityResponsePerSec = 12.0f;
	inline constexpr float MaxCellSpeedCmPerSec = 1200.0f;
	inline constexpr float BorderVelocityDampingPerSec = 4.0f;
	inline constexpr float BrushDisplacementRatio = 0.5f;
	inline constexpr float BrushEraseRatePerSec = 8.0f;
	inline constexpr float PressureNormalizationCmPerSec = 600.0f;
	inline constexpr float PressureReferenceCellSizeCm = 50.0f;
	inline constexpr float OutputSpeedReferenceCmPerSec = 600.0f;
	inline constexpr float NoObstacleDistanceCm = 10000.0f;
	inline constexpr float ObstacleBoundaryBandCells = 1.5f;
	inline constexpr float TerrainUnknownElevationCm = -1.0e30f;
	inline constexpr float TerrainUnknownElevationThresholdCm = -1.0e29f;
	inline constexpr uint32 FlowNoiseSeed = 0x2F6E2B1Du;
	inline constexpr uint32 AmbientNoiseSeed = 0x6B43A9B5u;
	inline constexpr float FlowNoiseScaleMultiplier = 1.0f;
	inline constexpr float AmbientNoiseScaleMultiplier = 1.7f;
	inline constexpr double NoiseTimeWrapPeriod = 65536.0;
	inline constexpr int32 TransmittanceLevelCount = 4;
	inline constexpr int32 TransmittanceResolutionDivisor = 4;
	inline constexpr int32 MinimumTransmittanceResolution = 16;
	inline constexpr int32 MinimumTransmittanceMarchSteps = 4;
	inline constexpr int32 MaximumTransmittanceMarchSteps = 24;
	inline constexpr float TransmittanceEdgeFadeFraction = 0.24f;
	inline constexpr float TransmittanceCoverageScale = 2.5f;
	inline constexpr float TransmittanceHeightScaleRatio = 0.68f;
	inline constexpr float TransmittanceTopFadeRatio = 0.15f;
}

enum class ETDFluidGpuSimMode : uint8
{
	FluidSim,
	SimplePainter
};

struct FTDFluidGpuStampInput
{
	FVector2D CenterPlaneCm = FVector2D::ZeroVector;
	FVector2D VelocityPlaneCmPerSec = FVector2D::ZeroVector;
	float RadiusCm = 50.0f;
	float DensityStrength = 1.0f;
	float VelocityStrength = 1.0f;
	float RadialVelocityCmPerSec = 0.0f;
	float HeatStrength = 0.0f;
	bool bWritesSelectiveDensity = false;
};

struct FTDFluidGpuObstacleInput
{
	FVector2D CenterPlaneCm = FVector2D::ZeroVector;
	float RadiusCm = 100.0f;
	FVector2D BoxHalfExtentsCm = FVector2D(100.0, 100.0);
	bool bIsBox = false;
};

struct FTDFluidGpuTerrainCellUpdate
{
	FIntPoint Cell = FIntPoint::ZeroValue;
	float ElevationCm = 0.0f;
};

struct FTDFluidGpuSimulationSettings
{
	ETDFluidGpuSimMode SimMode = ETDFluidGpuSimMode::FluidSim;
	int32 Resolution = 64;
	FVector2D ExtentCm = FVector2D(4000.0, 4000.0);
	float VelocityDamping = 0.05f;
	float DensityDissipation = 0.15f;
	int32 PressureIterations = 6;
	float VorticityStrength = 1.0f;
	float TerrainSlopeGravity = 400.0f;
	float AdvectionSpeedScale = 1.0f;
	float BorderFadeRatio = 0.1f;
	int32 MaxStampsPerTick = TDFluidGpuSolverConstants::MaximumStampsPerTick;
	float AmbientDensity = 0.0f;
	float AmbientRefillPerSec = 0.0f;
	float FlowNoiseAccelerationCmPerSec2 = 0.0f;
	float FlowNoiseScaleCm = 200.0f;
	float FlowNoiseSpeed = 0.12f;
	float AmbientNoiseAmount = 0.0f;
	float BuoyancyCmPerSec2 = 0.0f;
	float OutflowBandRatio = 0.0f;
	float OutflowRatePerSec = 0.0f;
	bool bUsesHeatChannel = false;
	float HeatCoolingPerSec = 2.0f;
	float HeatBuoyancyCmPerSec2 = 0.0f;
	bool bReflectsAtBoundaries = false;
	float EdgeBounciness = 0.0f;
	float BorderFadeExponent = 1.0f;
	float VortexTangentialAccelerationCmPerSec2 = 0.0f;
	float VortexRadialDriftCmPerSec = 0.0f;
	float VortexRadiusRatio = 0.35f;
};

struct FTDFluidGpuTransmittanceInput
{
	bool bIsEnabled = false;
	int32 LightMode = 0;
	FVector3f LightPositionLocalCm = FVector3f::ZeroVector;
	FVector3f LightDirectionLocal = FVector3f(0.0f, 0.0f, -1.0f);
	float VolumeHeightCm = 150.0f;
	bool bIsSymmetric = false;
	float DensityScale = 0.035f;
	float SoftFalloffCm = 28.0f;
	int32 MarchStepCount = 12;
};

struct FTDFluidGpuActorInput
{
	uint64 ActorKey = 0;
	FTDFluidGpuSimulationSettings Settings;
	FVector2D WindowCenterPlaneCm = FVector2D::ZeroVector;
	FIntPoint ShiftCells = FIntPoint::ZeroValue;
	FVector2D PlaneUpDirection = FVector2D::ZeroVector;
	float DeltaTimeSeconds = 0.0f;
	double SimulationTimeSeconds = 0.0;
	bool bShouldResetState = false;
	bool bHasTerrainElevation = false;
	bool bHasObstacleListChanged = false;
	TArray<FTDFluidGpuStampInput> Stamps;
	TArray<FTDFluidGpuObstacleInput> Obstacles;
	TArray<FTDFluidGpuTerrainCellUpdate> TerrainElevationUpdates;
	FTextureRenderTargetResource* DensityPressureOutput = nullptr;
	FTextureRenderTargetResource* VelocityOutput = nullptr;
	FTextureRenderTargetResource* AuxOutput = nullptr;
	FTextureRenderTargetResource* TransmittanceOutput = nullptr;
	FTDFluidGpuTransmittanceInput Transmittance;
};

struct FTDFluidGpuFrameInputs
{
	uint32 FrameNumber = 0;
	TArray<FTDFluidGpuActorInput> Actors;
	TArray<uint64> ReleasedActorKeys;
};
