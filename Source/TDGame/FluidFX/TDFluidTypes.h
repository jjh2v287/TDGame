#pragma once

#include "CoreMinimal.h"
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
	PortalVortex
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "0.0", ClampMax = "20000.0"))
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
