#pragma once

#include "CoreMinimal.h"
class UNiagaraDataChannelAsset;

#include "TDFluidTypes.generated.h"

UENUM(BlueprintType)
enum class ETDFluidSimMode : uint8
{
	SimplePainter,
	FluidSim
};

UENUM(BlueprintType)
enum class ETDFluidSolverBackend : uint8
{
	Cpu,
	Gpu,
	Auto
};

UENUM(BlueprintType)
enum class ETDFluidBrushChannel : uint8
{
	ChannelR_Mud,
	ChannelG_Blood,
	ChannelB_Fire,
	ChannelA_Water
};

UENUM(BlueprintType)
enum class ETDFluidBrushSource : uint8
{
	Other,
	PointArray,
	DataChannel,
	Fragment,
	Pivot,
	Bone,
	TrackPoint,
	Pointer,
	Shared,
	Field,
	Rain
};

UENUM(BlueprintType)
enum class ETDFluidVelocityOnlySources : uint8
{
	Off,
	PointArrays,
	DataChannel,
	Fragments,
	PointArraysAndFragments,
	All,
	Fields
};

UENUM(BlueprintType)
enum class ETDFluidPresetKind : uint8
{
	Custom,
	Water,
	Mud,
	Blood,
	Fire,
	FootprintPainter,
	Smoke,
	River,
	Sea,
	Torch,
	Campfire,
	CampfireSmoke,
	Explosion,
	ExplosionSmoke,
	ExplosionGroundDust,
	Candle,
	Fireball,
	PlasmaExplosion,
	SteamPuff,
	Vortex,
	InvertedVortex,
	PortalVortex,
	TileWaves,
	RisingSmokeColumn,
	FallingSmokeColumn,
	FallingThinSmokeColumn,
	SmokePiston,
	SmokeHeterogeneous,
	VortexHeterogeneous,
	SmokeCameraFacing,
	FloorSmokeCapture,
	Flamethrower,
	SnowAvalanche,
	MassInputBench,
	FogMist,
	FogMistStir,
	FogTileSim,
	SnowTrack,
	SandTrack,
	SnowDust,
	SandDust,
	ViscousMud,
	ViscousBlood,
	LavaTile,
	LavaLocal,
	InputBrushBench,
	InputFieldBench,
	SmokeKernelPressure,
	SmokeWindDrift,
	SmokeAnchoredCauldron,
	FoliageWalk,
	FoliagePainter,
	ReedShallows,
	DenseCreek,
	DenseLake,
	DenseSea,
	DenseTank,
	ShipWakeSea,
	WaterPool,
	Fountain,
	Cauldron,
	Waterfall,
	LiquidSphereClear,
	LiquidSphereTurbulent,
	BloodOrb,
	WaterySandFlow,
	RainPond,
	RainAccumulationBench,
	CloudPaint,
	CloudCyclone,
	CloudStormWall,
	CloudWrapField
};

UENUM(BlueprintType)
enum class ETDFluidGpuPressureSolverMode : uint8
{
	Auto,
	Jacobi,
	MultigridFrom64
};

UENUM(BlueprintType)
enum class ETDFluidAreaAddressMode : uint8
{
	Clamp,
	Wrap
};

UENUM(BlueprintType)
enum class ETDFluidWaterGpuPressureMode : uint8
{
	Default,
	HalfMultigrid,
	HalfKernelFeedback
};

UENUM(BlueprintType)
enum class ETDFluidPressureMethod : uint8
{
	PoissonSolve,
	KernelFeedback
};

UENUM(BlueprintType)
enum class ETDFluidTrackDeformShadowPolicy : uint8
{
	NoShadow,
	Rigid,
	Auto
};

UENUM(BlueprintType)
enum class ETDFluidObjectSizeMode : uint8
{
	Fixed,
	ActorScale,
	Bounds
};

UENUM(BlueprintType)
enum class ETDFluidPointerInputMode : uint8
{
	Off,
	Mouse,
	Touch,
	MultiTouch
};

UENUM(BlueprintType)
enum class ETDFluidFieldInputLayout : uint8
{
	Density,
	DensityVelocity,
	Velocity,
	CollisionMask,
	MeshDistance
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidBrushStamp
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector WorldVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float RadiusCm = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	float DensityStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float VelocityStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	float RadialVelocityCmPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidBrushChannel Channel = ETDFluidBrushChannel::ChannelR_Mud;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	float RippleImpulseCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeatStrength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float RingInnerRadiusRatio = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float RingSideArcWeight = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float RingInnerEraseRatePerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidBrushSource Source = ETDFluidBrushSource::Other;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float ShapeAspectRatio = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float ShapeHardness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector ShapeMajorAxis = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bHasGroundElevation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	double GroundElevationCm = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bAddsDensity = false;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidSdfObstacle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector WorldCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0"))
	float RadiusCm = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector2D BoxHalfExtentsCm = FVector2D(100.0f, 100.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	bool bIsBox = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector BoxHalfExtents3DCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FRotator WorldRotation = FRotator::ZeroRotator;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidFlowGuidePoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float HalfWidthCm = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	float FlowSpeedCmPerSec = 200.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidFlowGuide
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	TArray<FTDFluidFlowGuidePoint> Points;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidWaterRegion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector2D MinXY = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector2D MaxXY = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	float SurfaceHeightCm = 0.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidSimulationConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidSimMode SimMode = ETDFluidSimMode::FluidSim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "16", ClampMax = "2048"))
	int32 GridResolution = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector2D SimExtentCm = FVector2D(4000.0f, 4000.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float VelocityDamping = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float DensityDissipation = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1", ClampMax = "32"))
	int32 PressureIterations = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float VorticityStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0"))
	float TerrainSlopeGravity = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.01", ClampMax = "10.0"))
	float AdvectionSpeedScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "0.49"))
	float BorderFadeRatio = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1", ClampMax = "512"))
	int32 MaxPointsPerTick = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AmbientDensity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float AmbientRefillPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-10.0", ClampMax = "10.0"))
	float ActorBrushDensityStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-1200.0", ClampMax = "1200.0"))
	float ActorBrushPunctureCmPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
	float FlowNoiseAccelerationCmPerSec2 = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0", ClampMax = "100000.0"))
	float FlowNoiseScaleCm = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float FlowNoiseSpeed = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AmbientNoiseAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "-20000.0", ClampMax = "20000.0"))
	float BuoyancyCmPerSec2 = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float OutflowBandRatio = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float OutflowRatePerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	bool bEnableWaterLayer = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	bool bOpenWaterBorders = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	bool bTreatDryCellsAsSolid = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "1.0", ClampMax = "100000.0"))
	float WaterDepthReferenceCm = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float FlowGuideResponsePerSec = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "10000.0"))
	float FlowGuideBankFalloffCm = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlowGuideBankSpeedRatio = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float RapidsWhitewaterPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float WhitewaterFromVorticity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float WhitewaterVorticityThresholdPerSec = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "2000.0"))
	float RippleWaveSpeedCmPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float RippleDampingPerSec = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.01", ClampMax = "1000.0"))
	float RippleHeightReferenceCm = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float RippleFlowAdvection = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "-100.0", ClampMax = "100.0"))
	float ActorBrushRippleImpulseCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "1.0", ClampMax = "100000.0"))
	float CoastWaveDepthSpacingCm = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.1", ClampMax = "1000.0"))
	float CoastWavePeriodSeconds = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "100000.0"))
	float CoastWaveMaxDepthCm = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float SwashHeightCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float BankWetHeightCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.01", ClampMax = "10000.0"))
	float WetnessDryingSeconds = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WetnessFromWhitewater = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	bool bEnableWaterWetness = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	bool bUseWhitewaterDissipation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float WhitewaterDissipation = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater")
	bool bEnableDenseWater = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "50.0", ClampMax = "3000.0"))
	float DenseWaterGravityCmPerSec2 = 980.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DenseWaterGapFillTendency = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DenseWaterPeakAvoidance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "0.1", ClampMax = "50.0"))
	float DenseWaterPeakSoftnessCm = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "0.0", ClampMax = "5000.0"))
	float DenseWaterSdfRepelCmPerSec2 = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float DenseWaterSdfRepelBandCm = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float DenseWaterLinearDragPerSec = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DenseWaterBedFriction = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "0.01", ClampMax = "50.0"))
	float DenseWaterWetThresholdCm = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "0.001", ClampMax = "5.0"))
	float DenseWaterDryDepthCm = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float DenseWaterFilmDrainCmPerSec = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "10.0", ClampMax = "2000.0"))
	float DenseWaterMaxFlowDepthCm = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "10.0", ClampMax = "2000.0"))
	float DenseWaterMaxSpeedCmPerSec = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float DenseWaterCourantNumber = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "1", ClampMax = "16"))
	int32 DenseWaterMaxSubsteps = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DenseWaterMomentumAdvection = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DenseWater", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float DenseWaterReservoirRelaxPerSec = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidSolverBackend SolverBackend = ETDFluidSolverBackend::Cpu;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0", ClampMax = "256"))
	int32 CpuFallbackResolution = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat")
	bool bEnableHeatChannel = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float HeatCoolingPerSec = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat", meta = (ClampMin = "0.0", ClampMax = "20000.0"))
	float HeatBuoyancyCmPerSec2 = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds")
	bool bReflectsAtBoundaries = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds")
	ETDFluidAreaAddressMode AreaAddressMode = ETDFluidAreaAddressMode::Clamp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EdgeBounciness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds", meta = (ClampMin = "0.25", ClampMax = "8.0"))
	float BorderFadeExponent = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex", meta = (ClampMin = "-20000.0", ClampMax = "20000.0"))
	float VortexTangentialAccelerationCmPerSec2 = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex", meta = (ClampMin = "-1200.0", ClampMax = "1200.0"))
	float VortexRadialDriftCmPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float VortexRadiusRatio = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SolverOptions", meta = (ClampMin = "-1200.0", ClampMax = "1200.0"))
	FVector DirectVelocityCmPerSec = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SolverOptions")
	bool bDirectVelocityInWorldSpace = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SolverOptions")
	bool bAnchorsDensityToActor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SolverOptions", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SimAreaMotionVelocityScale = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SolverOptions", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float FlowAmplitudeNoiseDampingPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SolverOptions", meta = (ClampMin = "1.0", ClampMax = "100000.0"))
	float FlowAmplitudeNoiseScaleCm = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SolverOptions")
	bool bRandomizeNoiseOffsetPerActor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Solver", meta = (ClampMin = "1.0"))
	float MaxCellSpeedCmPerSec = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Solver", meta = (ClampMin = "0.0"))
	float BrushVelocityResponsePerSec = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Solver", meta = (ClampMin = "0.0"))
	float BrushDisplacementRatio = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Solver", meta = (ClampMin = "0.0"))
	float BrushEraseRatePerSec = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GpuPressure")
	ETDFluidGpuPressureSolverMode GpuPressureSolverMode = ETDFluidGpuPressureSolverMode::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GpuPressure", meta = (ClampMin = "1", ClampMax = "4"))
	int32 GpuMultigridCycles = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GpuPressure")
	ETDFluidWaterGpuPressureMode WaterGpuPressureMode = ETDFluidWaterGpuPressureMode::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GpuPressure", meta = (ClampMin = "2", ClampMax = "7"))
	int32 PressureKernelRadiusTexels = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GpuPressure", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PressureKernelFeedback = 0.98f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GpuPressure", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float PressureKernelScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GpuPressure")
	ETDFluidPressureMethod PressureMethod = ETDFluidPressureMethod::PoissonSolve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GpuPressure", meta = (ClampMin = "1", ClampMax = "7"))
	int32 PressureKernelDivergenceRadiusTexels = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GpuPressure", meta = (ClampMin = "1", ClampMax = "4"))
	int32 PressureKernelPassesPerProjection = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	bool bOutputObstacleDistance = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "20.0", ClampMax = "2000.0"))
	float ObstacleDistanceOutputRangeCm = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FoliageBend")
	bool bEnableBendVectorChannel = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FoliageBend", meta = (ClampMin = "0.02", ClampMax = "2.0"))
	float BendVectorRiseSeconds = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FoliageBend", meta = (ClampMin = "0.05", ClampMax = "10.0"))
	float BendVectorRecoverySeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FoliageBend", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BendFromPainterDensity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FoliageBend", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float BendFromRippleSlope = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput", meta = (ClampMin = "1", ClampMax = "16000"))
	int32 GpuMaxStampsPerTick = 16000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput", meta = (ClampMin = "1", ClampMax = "256"))
	int32 GpuMaxStampsPerBin = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput", meta = (ClampMin = "1", ClampMax = "4"))
	int32 MassInputBufferDivisor = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MassSplatRadiusCapCells = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrackBank", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float TrackBankStrength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrackBank", meta = (ClampMin = "2.0", ClampMax = "60.0"))
	float TrackBankBlurRadiusCm = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrackBank", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TrackBankHeightLimit = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrackBank")
	bool bWritesPainterVelocity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float DensityAccumulationPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "1.0", ClampMax = "8.0"))
	float DenseDensityCeiling = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DenseGrowthThreshold = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "1200.0"))
	float FlowNoiseSpeedMaskReferenceCmPerSec = 0.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidInputBrushSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0"))
	float PivotDensityStrength = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0"))
	float BoneDensityStrength = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0"))
	float ParticleDensityStrength = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0"))
	float MouseBrushDensityStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0"))
	float TeleportSpeedCmPerSec = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float RingInnerRadiusRatio = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float RingSideArcWeight = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float RingInnerEraseRatePerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MassPointDensityStrength = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float MassPointVelocityStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MassPointHeatStrength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float MassPointRadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush")
	ETDFluidVelocityOnlySources VelocityOnlySources = ETDFluidVelocityOnlySources::Off;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush")
	bool bShouldInvertInputDensity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "1200.0"))
	float DensityFadeBelowSpeedCmPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float BrushHeightFadeDistanceCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float GroundSlabThicknessCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float BoneBrushRadiusOverrideCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "400.0"))
	float PivotBrushRadiusOverrideCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float BoneStampAspectRatio = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float BrushHardness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush")
	bool bStampsSlowBones = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "3000.0"))
	float BoneStampMaxSpeedCmPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float VehicleBrushRadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float VehicleStampAspectRatio = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float HullWakeVelocityStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float HullWakeDensityStrength = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float HullWakeRippleImpulseCm = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "2000.0"))
	float HullWakeMinSpeedCmPerSec = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush")
	bool bInterpolateStrokes = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float StrokeSpacingRatio = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float MassPointRippleImpulseCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float BrushVelocityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float GlobalBrushRadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float PivotBrushRadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float BoneBrushRadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float PointerBrushRadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float ParticleBrushRadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float FragmentBrushRadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float SharedBrushRadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush")
	ETDFluidObjectSizeMode ObjectSizeMode = ETDFluidObjectSizeMode::Fixed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float ObjectSizeRadiusRatio = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush")
	bool bHardnessScalesWithObjectSize = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.25", ClampMax = "4.0"))
	float BrushVelocityExponent = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "10.0", ClampMax = "1200.0"))
	float BrushVelocityReferenceCmPerSec = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "5000.0"))
	float BrushVelocityClampCmPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MotionLeadRatio = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PositionJitterRatio = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush")
	bool bJitterScalesWithSpeed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DensityNoiseAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "1.0", ClampMax = "10000.0"))
	float DensityNoiseScaleCm = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VelocityNoiseAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "1.0", ClampMax = "10000.0"))
	float VelocityNoiseScaleCm = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0", ClampMax = "65535"))
	int32 BrushNoiseSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush")
	bool bAdditiveStrokeBlend = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.1", ClampMax = "60.0"))
	float AdditiveDensityRatePerSec = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush")
	bool bInterpolatePivotAndPointerStrokes = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float InputPersistenceSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float LargeObjectExcludeRatio = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush")
	bool bMovePivotToBoundsBottom = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush")
	bool bReceivesSharedInput = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float SharedInputDensityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputBrush", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float SharedInputVelocityScale = 1.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidMassInputSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput")
	bool bAcceptMassInput = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput")
	FName MassInputChannelName = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput")
	bool bReadDataChannel = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput")
	TSoftObjectPtr<UNiagaraDataChannelAsset> DataChannel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput")
	bool bReadPreviousFrame = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput")
	bool bSearchIslandByWindowCenter = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput", meta = (ClampMin = "0", ClampMax = "16000"))
	int32 MaxDataChannelPointsPerTick = 10000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float DefaultPointRadiusCm = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput")
	bool bCollectObstacleComponents = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassInput", meta = (ClampMin = "0", ClampMax = "256"))
	int32 MaxCollectedObstacles = 32;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidFieldInputSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput")
	bool bAcceptFieldInputs = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput", meta = (ClampMin = "0", ClampMax = "4"))
	int32 MaxFieldInputs = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float FieldDensityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float FieldVelocityScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput", meta = (ClampMin = "0.0", ClampMax = "1200.0"))
	float FieldVelocityRangeCmPerSec = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput")
	bool bFieldCollisionEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput", meta = (ClampMin = "1.0", ClampMax = "400.0"))
	float FieldCollisionEdgeCm = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MeshFieldDensityStrength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput", meta = (ClampMin = "0.0", ClampMax = "1200.0"))
	float MeshFieldOutwardVelocityCmPerSec = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MeshFieldMotionVelocityStrength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput", meta = (ClampMin = "1.0", ClampMax = "400.0"))
	float MeshContourBandCm = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MeshContourNoiseAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput", meta = (ClampMin = "1.0", ClampMax = "10000.0"))
	float MeshContourNoiseScaleCm = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FieldInput")
	bool bMeshFieldFillsInterior = false;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidRuntimeOpsSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RuntimeOps", meta = (ClampMin = "0.0"))
	float SleepDistanceRatio = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RuntimeOps", meta = (ClampMin = "1.0"))
	float InteractionVolumeHalfHeightCm = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RuntimeOps", meta = (ClampMin = "0.0"))
	double NiagaraCacheRefreshIntervalSeconds = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RuntimeOps", meta = (ClampMin = "0", ClampMax = "512"))
	int32 MaxGroundTracesPerTick = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RuntimeOps", meta = (ClampMin = "50.0", ClampMax = "5000.0"))
	float GroundTraceDepthCm = 500.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidSurfaceRenderSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SurfaceRender")
	bool bUsesTrackDeformMesh = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SurfaceRender", meta = (ClampMin = "1000.0", ClampMax = "5000.0"))
	float TrackDeformMeshExtentCm = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SurfaceRender")
	ETDFluidTrackDeformShadowPolicy TrackDeformMeshShadowPolicy = ETDFluidTrackDeformShadowPolicy::NoShadow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SurfaceRender", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float TrackDeformMeshRecenterDistanceCm = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SurfaceRender")
	bool bUsesDecalOutput = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SurfaceRender", meta = (ClampMin = "10.0", ClampMax = "500.0"))
	float DecalBoxHeightCm = 100.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidWaterLodGridSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaterLodGrid")
	bool bEnableLodGrid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaterLodGrid", meta = (ClampMin = "10.0", ClampMax = "40.0"))
	float BaseQuadSizeCm = 18.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaterLodGrid", meta = (ClampMin = "1000.0", ClampMax = "8000.0"))
	float BaseRangeCm = 2400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaterLodGrid", meta = (ClampMin = "1", ClampMax = "10"))
	int32 LevelCount = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaterLodGrid", meta = (ClampMin = "4", ClampMax = "64"))
	int32 PatchQuads = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaterLodGrid", meta = (ClampMin = "0.55", ClampMax = "0.9"))
	float MorphStartRatio = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaterLodGrid", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float VertexWaveFilterRatio = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaterLodGrid", meta = (ClampMin = "20000", ClampMax = "1000000"))
	int32 TotalVertexBudget = 300000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaterLodGrid", meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float ReselectDistanceCm = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WaterLodGrid", meta = (ClampMin = "0.0", ClampMax = "500000.0"))
	float CoverageMarginCm = 0.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidWaterNearRippleSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "64", ClampMax = "512"))
	int32 Resolution = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "200.0", ClampMax = "1200.0"))
	float ExtentCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "10.0", ClampMax = "80.0"))
	float WaveSpeedCmPerSec = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float DampingPerSec = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "0.02", ClampMax = "0.4"))
	float EdgeFadeRatio = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float EdgeAbsorptionStrength = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float MovingImpulseCmPerSec = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float MovingReferenceSpeedCmPerSec = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "0.5", ClampMax = "4.0"))
	float MovingSpeedExponent = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float SourceRadiusScale = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float IdleContactAmplitudeCm = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float IdleContactHz = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "2.0", ClampMax = "50.0"))
	float IdleContactRadiusCm = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float MinWaterDepthCm = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxSourcesPerFrame = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NearRipple", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxSubsteps = 4;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidCellSample
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float PrimaryDensity = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float SelectiveDensity = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float Pressure = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float ObstacleSdf = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FVector2D Velocity = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FVector2D VelocityCmPerSec = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FVector2D TerrainSlope = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float WaterDepthCm = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float Wetness = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float FlowGuideWeight = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FVector2D FlowTargetVelocity = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float RippleHeightCm = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	bool bIsTerrainKnown = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float Heat = 0.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidWaterPointSample
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	bool bIsInsideWindow = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	bool bHasWaterSurface = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	bool bIsTerrainKnown = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float WaterSurfaceHeightCm = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float WaterDepthCm = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FVector2D VelocityCmPerSec = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	FVector2D FlowTargetVelocityCmPerSec = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float FlowGuideWeight = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float Whitewater = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float RippleHeightCm = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float ObstacleSdfCm = 10000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	float Wetness = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FluidFX")
	int32 FrameAge = 0;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidShadowCapsule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector StartWorld = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FVector EndWorld = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.1"))
	float RadiusCm = 10.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidShadowCapsuleBonePair
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FName StartBoneName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	FName EndBoneName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.1"))
	float RadiusCm = 8.0f;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidVolumeRenderSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume")
	bool bEnableTransmittance = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DensityScale = 0.035f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float SoftFalloffCm = 28.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "4", ClampMax = "24"))
	int32 MarchStepCount = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.0", ClampMax = "0.99"))
	float CapsuleShadowStrength = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.0"))
	float VolumeFadeEndCm = 4200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "1.0"))
	float VolumeFadeLengthCm = 1600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float VolumeSizeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume")
	FVector VolumePositionOffsetCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume")
	FLinearColor HeatEmissionColor = FLinearColor(1.0f, 0.25f, 0.05f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.0"))
	float HeatEmissionIntensity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float HeatEmissionExponent = 2.0f;
};

struct FTDFluidVolumeLightState
{
	int32 Mode = 0;
	float RadiusCm = 0.0f;
	FVector PositionWorld = FVector::ZeroVector;
	FVector DirectionWorld = FVector::ForwardVector;
	FLinearColor Color = FLinearColor::Black;
};
