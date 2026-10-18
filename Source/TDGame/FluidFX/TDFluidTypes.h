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
	Smoke
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
struct TDGAME_API FTDFluidSimulationConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX")
	ETDFluidSimMode SimMode = ETDFluidSimMode::FluidSim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FluidFX", meta = (ClampMin = "16", ClampMax = "256"))
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
};
