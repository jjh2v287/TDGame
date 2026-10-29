#pragma once

#include "CoreMinimal.h"
#include "TDFluidGpuSolverOptionSettings.h"
#include "TDFluidGpuDenseWater.h"
#include "TDFluidGpuStateCacheTypes.h"

class FTextureRenderTargetResource;

namespace TDFluidGpuSolverConstants
{
	inline constexpr int32 MinimumResolution = 16;
	inline constexpr int32 MaximumResolution = 2048;
	inline constexpr int32 CpuMaximumResolution = 256;
	inline constexpr int32 WrapAreaMaximumResolution = 256;
	inline constexpr uint8 WrapAreaAddressModeRequest = 1;
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
	inline constexpr float MassInputDensityScale = 16777216.0f;
	inline constexpr float MassInputVelocityScale = 8.0f;
	inline constexpr float MassInputWeightScale = 4096.0f;
	inline constexpr float MassInputQuantizedLimit = 2147483520.0f;
	inline constexpr int32 MassInputChannelCount = 5;
	inline constexpr int32 MassInputCounterCount = 4;
	inline constexpr int32 DefaultMassInputDivisor = 2;
	inline constexpr int32 DefaultMassSplatRadiusCapCells = 8;
	inline constexpr int32 MaximumMassSplatRadiusCapCells = 16;
	inline constexpr int32 MaximumMassPointsPerUpload = 16000;
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
	inline constexpr int32 MaximumWaterProbesPerFrame = 64;
	inline constexpr float WaterNoSurfaceHeightCm = -1.0e30f;
	inline constexpr float WaterNoSurfaceHeightThresholdCm = -1.0e29f;
	inline constexpr int32 MaximumWaterRippleSubsteps = 8;
	inline constexpr float WaterRippleSubstepSafety = 0.5f;
	inline constexpr float WaterUnknownTerrainSdfCm = 10000.0f;
	inline constexpr float WaterNoSurfaceSdfCm = -1.0f;
	inline constexpr float WaterMinimumTerrainSlope = 0.05f;
	inline constexpr float WaterSdfLimitCm = 10000.0f;
	inline constexpr float WaterTerrainHeightScaleCm = 1000.0f;
	inline constexpr float WaterTerrainHeightLimit = 8.0f;
	inline constexpr float WaterRippleOvershootRatio = 0.25f;
	inline constexpr int32 WaterPowerOfTwoTierMaximumSmallResolution = 64;
	inline constexpr int32 WaterExactTierGranularity = 64;
	inline constexpr int32 WaterExactTierMaximumResolution = 256;
	inline constexpr int32 WaterHalfPressureMinimumResolution = 512;
	inline constexpr int32 WaterQuarterFieldMinimumResolution = 1024;
	inline constexpr int32 MaximumGpuOnlyWaterRippleSubsteps = 16;
	inline constexpr float WaterRippleCourantLimitRaw = 32.0f;
	inline constexpr float WaterRippleCourantTargetRaw = 28.8f;
	inline constexpr float WaterHalfPressureDirichletBaseScale = 0.5f;
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
	float RippleImpulseCm = 0.0f;
	float RingInnerRadiusRatio = 0.0f;
	float RingSideArcWeight = 0.3f;
	float RingInnerEraseRatePerSec = 0.0f;
	float ShapeAspectRatio = 1.0f;
	float ShapeHardness = 0.0f;
	FVector2D ShapeMajorAxisPlane = FVector2D::ZeroVector;
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
	int32 MaxStampsPerBin = TDFluidGpuSolverConstants::MaximumStampsPerBin;
	bool bAcceptsMassInput = false;
	bool bMassInputWritesSelectiveDensity = false;
	int32 MassInputDivisor = TDFluidGpuSolverConstants::DefaultMassInputDivisor;
	int32 MassSplatRadiusCapCells = TDFluidGpuSolverConstants::DefaultMassSplatRadiusCapCells;
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
	float MaxCellSpeedCmPerSec = TDFluidGpuSolverConstants::MaxCellSpeedCmPerSec;
	float BrushVelocityResponsePerSec = TDFluidGpuSolverConstants::BrushVelocityResponsePerSec;
	float BrushDisplacementRatio = TDFluidGpuSolverConstants::BrushDisplacementRatio;
	float BrushEraseRatePerSec = TDFluidGpuSolverConstants::BrushEraseRatePerSec;
	float TrackBankStrength = 0.0f;
	float TrackBankBlurRadiusCm = 15.0f;
	float TrackBankHeightLimit = 0.5f;
	bool bWritesPainterVelocity = false;
	uint8 PressureSolverModeRequest = 0;
	uint8 AreaAddressModeRequest = 0;
	int32 MultigridCyclesRequest = TDFluidGpuSolverConstants::DefaultMultigridCycles;
	FTDFluidGpuSolverOptionSettings SolverOptions;
	bool bUsesWaterLayer = false;
	bool bOpenWaterBorders = true;
	bool bTreatDryCellsAsSolid = true;
	float WaterDepthReferenceCm = 300.0f;
	float FlowGuideResponsePerSec = 3.0f;
	float WhitewaterFromVorticity = 0.0f;
	float WhitewaterVorticityThresholdPerSec = 2.0f;
	float RapidsWhitewaterPerSec = 0.0f;
	float RippleWaveSpeedCmPerSec = 0.0f;
	float RippleDampingPerSec = 1.2f;
	float RippleHeightReferenceCm = 8.0f;
	float RippleFlowAdvection = 1.0f;
	bool bUsesFloat32WaterVelocity = false;
	bool bUsesFloat32Ripple = false;
	uint8 WaterPressureModeRequest = 0;
	int32 WaterKernelRadius = 4;
	float WaterKernelFeedback = 0.98f;
	float WaterKernelScale = 1.0f;
	int32 WaterMultigridCycles = 1;
	bool bUsesWaterWetness = false;
	bool bUsesWhitewaterDissipation = false;
	float WhitewaterDissipation = 0.0f;
	float BankWetHeightCm = 0.0f;
	float WetnessFromWhitewater = 0.5f;
	float SwashHeightCm = 0.0f;
	float CoastWavePeriodSeconds = 6.0f;
	float WetnessDryingSeconds = 8.0f;
	bool bWritesObstacleDistanceAux = false;
	float ObstacleDistanceOutputRangeCm = 400.0f;
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

struct FTDFluidGpuWaterGuideCellUpdate
{
	FIntPoint FieldCell = FIntPoint::ZeroValue;
	float SurfaceHeightCm = TDFluidGpuSolverConstants::WaterNoSurfaceHeightCm;
	FVector2f TargetVelocityCmPerSec = FVector2f::ZeroVector;
	float GuideWeight = 0.0f;
	float GuideDescent = 0.0f;
};

struct alignas(16) FTDFluidGpuWaterProbeSample
{
	FVector2f VelocityCmPerSec = FVector2f::ZeroVector;
	FVector2f FlowTargetVelocityCmPerSec = FVector2f::ZeroVector;
	float WaterSurfaceHeightCm = TDFluidGpuSolverConstants::WaterNoSurfaceHeightCm;
	float WaterDepthCm = 0.0f;
	float Whitewater = 0.0f;
	float SelectiveDensity = 0.0f;
	float RippleHeightCm = 0.0f;
	float FlowGuideWeight = 0.0f;
	float ObstacleSdfCm = TDFluidGpuSolverConstants::NoObstacleDistanceCm;
	float TerrainElevationCm = TDFluidGpuSolverConstants::TerrainUnknownElevationCm;
	float Wetness = 0.0f;
	uint32 ProbeSlot = 0;
	uint32 Flags = 0;
	uint32 Padding0 = 0;
};
static_assert(sizeof(FTDFluidGpuWaterProbeSample) == 64, "FTDFluidGpuWaterProbeSample must match the 64 byte shader layout.");
static_assert(offsetof(FTDFluidGpuWaterProbeSample, WaterSurfaceHeightCm) == 16, "FTDFluidGpuWaterProbeSample::WaterSurfaceHeightCm must stay at byte 16.");
static_assert(offsetof(FTDFluidGpuWaterProbeSample, ProbeSlot) == 52, "FTDFluidGpuWaterProbeSample::ProbeSlot must stay at byte 52.");

struct FTDFluidGpuMassPointInput
{
	FVector2D CenterPlaneCm = FVector2D::ZeroVector;
	FVector2D VelocityPlaneCmPerSec = FVector2D::ZeroVector;
	float RadiusCm = 0.0f;
	float DensityStrength = 0.0f;
	float HeatStrength = 0.0f;
	bool bWritesVelocity = true;
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
	bool bRefreshesObstacleDistanceAux = false;
	TArray<FTDFluidGpuStampInput> Stamps;
	TArray<FTDFluidGpuMassPointInput> MassPoints;
	TArray<FTDFluidGpuObstacleInput> Obstacles;
	TArray<FTDFluidGpuTerrainCellUpdate> TerrainElevationUpdates;
	FTextureRenderTargetResource* DensityPressureOutput = nullptr;
	FTextureRenderTargetResource* VelocityOutput = nullptr;
	FTextureRenderTargetResource* AuxOutput = nullptr;
	FTextureRenderTargetResource* TransmittanceOutput = nullptr;
	FTDFluidGpuTransmittanceInput Transmittance;
	FIntPoint WaterFieldOrigin = FIntPoint::ZeroValue;
	float WaterTerrainReferenceZCm = 0.0f;
	TArray<FTDFluidGpuWaterGuideCellUpdate> WaterGuideUpdates;
	TArray<FTDFluidGpuTerrainCellUpdate> WaterTerrainUpdates;
	TArray<FIntPoint> WaterProbeCells;
	int32 WaterProbeFirstSlot = 0;
	FTextureRenderTargetResource* WaterSurfaceOutput = nullptr;
	bool bUpdatesWaterWetness = false;
	bool bResetsWaterWetness = false;
	float WaterWetnessRetainFactor = 1.0f;
	float WaterSwashReachCm = 0.0f;
};

struct FTDFluidGpuFrameInputs
{
	uint32 FrameNumber = 0;
	TArray<FTDFluidGpuActorInput> Actors;
	TArray<uint64> ReleasedActorKeys;
};
