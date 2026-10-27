#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "TDFluidGpuMassInput.h"

struct FTDFluidMassInputPoint
{
	FVector WorldLocation = FVector::ZeroVector;
	FVector WorldVelocity = FVector::ZeroVector;
	float RadiusCm = 0.0f;
	float DensityStrength = 1.0f;
	float HeatStrength = 0.0f;
};

struct FTDFluidMassInputTarget
{
	uint64 ActorKey = 0;
	FName ChannelName;
	FVector WindowCenterWorld = FVector::ZeroVector;
	FVector AxisU = FVector(1.0, 0.0, 0.0);
	FVector AxisV = FVector(0.0, 1.0, 0.0);
	FVector AxisN = FVector(0.0, 0.0, 1.0);
	FVector2D PlaneCoordinateOffsetCm = FVector2D::ZeroVector;
	double PlaneDepthCm = 0.0;
	double SlabHalfDepthCm = 0.0;
	bool bIsWorldFacing = true;
	float DensityStrength = 0.0f;
	float VelocityStrength = 0.0f;
	float HeatStrength = 0.0f;
	float RadiusScale = 1.0f;
	float MaxCellSpeedCmPerSec = 0.0f;
	int32 RadiusCapCells = TDFluidGpuSolverConstants::DefaultMassSplatRadiusCapCells;
	bool bWritesSelectiveDensity = false;
	bool bUsesHeat = false;
};

class TDGAME_API FTDFluidMassInputHub
{
public:
	void PublishTargets(TArray<FTDFluidMassInputTarget>&& InTargets);
	TArray<FTDFluidMassInputTarget> GetTargets() const;
	bool FindNearestTarget(FName ChannelName, const FVector& WorldLocation, FTDFluidMassInputTarget& OutTarget) const;
	FTDFluidGpuMassInputRegistry& GetRegistry() { return Registry; }

private:
	mutable FCriticalSection TargetsMutex;
	TArray<FTDFluidMassInputTarget> Targets;
	FTDFluidGpuMassInputRegistry Registry;
};

namespace TDFluidMassInput
{
	TDGAME_API bool IsAllowedByConsole();
}
