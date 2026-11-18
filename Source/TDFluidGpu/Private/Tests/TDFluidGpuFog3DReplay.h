#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "TDFluidGpuFog3D.h"

constexpr int32 TDFog3DReplayDumpFieldCount = 10;
constexpr int32 TDFog3DReplayFluidFieldIndex = 9;
constexpr int32 TDFog3DReplaySphereFrameFloatCount = 16;
constexpr uint8 TDFog3DReplayOperationRest = 1;
constexpr uint8 TDFog3DReplayOperationEmpty = 2;

enum class ETDFog3DReplayRegionKind : uint8
{
	All,
	Aabb,
	Halfspace,
	Slab,
	CellBox,
	Not,
	Union
};

struct FTDFog3DReplayRegion
{
	ETDFog3DReplayRegionKind Kind = ETDFog3DReplayRegionKind::All;
	FVector3d MinimumCm = FVector3d::ZeroVector;
	FVector3d MaximumCm = FVector3d::ZeroVector;
	FVector3d PointCm = FVector3d::ZeroVector;
	FVector3d Normal = FVector3d::ZeroVector;
	double DistanceMinimumCm = 0.0;
	double DistanceMaximumCm = 0.0;
	double HeightMinimumCm = 0.0;
	double HeightMaximumCm = 0.0;
	FIntVector CellMinimum = FIntVector::ZeroValue;
	FIntVector CellMaximum = FIntVector::ZeroValue;
	TArray<TSharedPtr<FTDFog3DReplayRegion>> Children;
};

struct FTDFog3DReplayStateOperation
{
	uint8 Operation = 0;
	TSharedPtr<FTDFog3DReplayRegion> Region;
};

enum class ETDFog3DReplayWindKind : uint8
{
	None,
	Uniform,
	VerticalOscillation,
	Vortex
};

struct FTDFog3DReplayWind
{
	ETDFog3DReplayWindKind Kind = ETDFog3DReplayWindKind::None;
	FVector3d UniformCmPerSec = FVector3d::ZeroVector;
	double AmplitudeCmPerSec = 0.0;
	double PeriodSeconds = 1.0;
	double TimeSeconds = 0.0;
	double VortexSpeedCmPerSec = 0.0;
	double VortexHalfPeriodCm = 1.0;
	double VortexSign = 1.0;
};

struct FTDFog3DReplaySphere
{
	FVector3d StartCm = FVector3d::ZeroVector;
	FVector3d EndCm = FVector3d::ZeroVector;
	double RadiusCm = 0.0;
	FVector3d VelocityCmPerSec = FVector3d::ZeroVector;
	double WindStrength = 0.0;
	double WindGain = 1.0;
	double ErosionPerSec = 0.0;
	FVector3d OcclusionAnchorCm = FVector3d::ZeroVector;
	bool bHasOcclusionAnchor = false;
};

struct FTDFog3DReplayObstacle
{
	FVector3d CenterCm = FVector3d::ZeroVector;
	FVector3d HalfExtentCm = FVector3d::ZeroVector;
	double YawDegrees = 0.0;
};

struct FTDFog3DReplayStep
{
	double DeltaSeconds = 1.0 / 60.0;
	FIntPoint WindowMinCell = FIntPoint::ZeroValue;
	FIntPoint PreviousWindowMinCell = FIntPoint::ZeroValue;
	bool bResets = false;
	bool bAppliesPressure = true;
	bool bAppliesRefill = true;
	bool bAppliesEdgeBand = true;
	FTDFog3DReplayWind Wind;
	TArray<FTDFog3DReplayStateOperation> Operations;
	TArray<FTDFog3DReplaySphere> Spheres;
	FString DumpName;
	TArray<int32> DumpFieldIndices;
	int32 RepeatCount = 1;
	bool bChangesObstacles = false;
	TArray<FTDFog3DReplayObstacle> Obstacles;
};

struct FTDFog3DReplay
{
	FString Name;
	int32 Resolution = 128;
	int32 Mode = 2;
	bool bUsesClampedTimeWindBlend = false;
	bool bOccludesInputBehindObstacles = false;
	float CellSizeCm = 15.625f;
	float LayerHeightCm = 30.0f;
	FTDFluidGpuFog3DTuning Tuning;
	FTDFluidGpuFog3DInflation Inflation;
	FIntPoint AnchorCell = FIntPoint::ZeroValue;
	TArray<FTDFog3DReplayObstacle> Obstacles;
	TArray<FTDFog3DReplayStep> Steps;
	int32 ComputeCount = 0;
	int32 DumpCount = 0;
	bool bHasGround = false;
	FIntPoint GroundSourceSize = FIntPoint::ZeroValue;
	FTDFluidGpuFog3DGroundUpload GroundUpload;
};

bool LoadTDFog3DReplay(const FString& FilePath, FTDFog3DReplay& OutReplay, FString& OutError);
FString GetTDFog3DReplayDirectory();
FString GetTDFog3DDumpDirectory();
FString GetTDFog3DAddendumReplayDirectory();
FString GetTDFog3DAddendumDumpDirectory();
FString GetTDFog3DStage1dReplayDirectory();
FString GetTDFog3DStage1dDumpDirectory();
FString GetTDFog3DStage2aReplayDirectory();
FString GetTDFog3DStage2aDumpDirectory();
bool LoadTDFog3DReplayManifestNames(const FString& Directory, TArray<FString>& OutReplayFileNames, FString& OutError);
const TArray<FTDFog3DReplayObstacle>& FindTDFog3DReplayActiveObstacles(const FTDFog3DReplay& Replay, int32 StepIndex);
FVector3d GetTDFog3DReplayWindowOffsetCm(const FTDFog3DReplay& Replay, const FIntPoint& WindowMinCell);
FVector3d GetTDFog3DReplayCellCenterCm(const FTDFog3DReplay& Replay, const FIntPoint& WindowMinCell, int32 LocalX, int32 LocalY, int32 Layer);
bool DoesTDFog3DReplayStepMoveWindow(const FTDFog3DReplayStep& Step);
bool IsTDFog3DReplayRegionContaining(const FTDFog3DReplayRegion& Region, const FVector3d& CenterCm, const FIntVector& LocalCell);
FTDFluidGpuFog3DFrameInput MakeTDFog3DReplayFrameInput(const FTDFog3DReplay& Replay, int32 StepIndex, int32 RepeatIndex);
bool MakeTDFog3DReplayStateOverride(const FTDFog3DReplay& Replay, int32 StepIndex, int32 RepeatIndex, FTDFluidGpuFog3DStateOverride& OutStateOverride);

#endif
