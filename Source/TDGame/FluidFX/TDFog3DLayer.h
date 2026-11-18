#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFog3DGround.h"
#include "FluidFX/TDFog3DTypes.h"
#include "TDFluidGpuFog3D.h"

class UTextureRenderTargetVolume;
struct FTDFog3DGroundReadbackJob;
struct FTDFog3DLayerTestAccess;
struct FTDFog3DLayerRhiTestAccess;

enum class ETDFog3DResetCause : uint8
{
	ZoneReentered,
	VolumetricFogReenabled
};

struct FTDFog3DRenderSlot
{
	FTDFluidGpuFog3DState State;
	std::atomic<int32> RejectedStepCount{0};
};

struct FTDFog3DClockDecision
{
	bool bStepsNow = false;
	float StepDeltaSeconds = 0.0f;
	float ElapsedSeconds = 0.0f;
};

struct FTDFog3DInputSample
{
	int32 SlotId = 0;
	FVector CurrentWorld = FVector::ZeroVector;
	float RadiusCm = 20.0f;
	float WindStrength = 1.0f;
	float WindGain = 1.0f;
	float ErosionPerSec = 0.0f;
	FVector OcclusionAnchorWorld = FVector::ZeroVector;
	bool bHasOcclusionAnchor = false;
};

struct FTDFog3DGroundStep
{
	bool bFollowsGround = false;
	bool bGroundChanged = false;
	FTextureRenderTargetResource* CaptureDepthTarget = nullptr;
	FTDFog3DGroundRect CaptureRect;
	float DecodeOffsetCm = 0.0f;
	float MissingHeightCm = 0.0f;
	FTextureRenderTargetResource* MaterialTarget = nullptr;
};

struct FTDFog3DGroundReadbackSample
{
	float HeightCm = 0.0f;
	bool bIsValid = false;
};

struct FTDFog3DStepRequest
{
	FVector PlayerWorld = FVector::ZeroVector;
	double FloorZ = 0.0;
	TArray<FTDFog3DInputSample> Inputs;
	TArray<FTDFluidSdfObstacle> RegisteredObstacles;
	FTDFog3DClockDecision Clock;
	FTDFog3DGroundStep Ground;
};

struct FTDFog3DObstacleKey
{
	FVector WorldCenter = FVector::ZeroVector;
	FVector HalfExtentsCm = FVector::ZeroVector;
	double YawDegrees = 0.0;

	bool operator==(const FTDFog3DObstacleKey& Other) const
	{
		return WorldCenter == Other.WorldCenter && HalfExtentsCm == Other.HalfExtentsCm && YawDegrees == Other.YawDegrees;
	}
};

struct FTDFog3DResetInputs
{
	bool bIsFirstStep = false;
	bool bWindowJumpedTooFar = false;
	bool bZoneReentered = false;
	bool bConfigurationChanged = false;
	bool bVolumetricFogReenabled = false;
	bool bSawNewRejection = false;
};

class TDGAME_API FTDFog3DLayer
{
public:
	FTDFog3DLayer();
	~FTDFog3DLayer();
	FTDFog3DLayer(const FTDFog3DLayer&) = delete;
	FTDFog3DLayer& operator=(const FTDFog3DLayer&) = delete;

	void Configure(const FTDFog3DSettings& InSettings, int32 QualityLevel, int32 Mode);
	void RequestReset(ETDFog3DResetCause Cause = ETDFog3DResetCause::ZoneReentered);
	FTDFog3DClockDecision AdvanceClock(float FrameDeltaSeconds);
	bool SubmitStep(const FTDFog3DStepRequest& Request, UTextureRenderTargetVolume& OutputTarget);
	void ForgetInputHistory();
	void Release();
	bool RequestGroundReadback(const TArray<FIntPoint>& Cells, float MissingHeightCm);
	bool TakeGroundReadback(TArray<FTDFog3DGroundReadbackSample>& OutSamples);
	bool HasGroundReadback() const { return GroundReadbackJob.IsValid(); }

	bool HasSubmittedWindow() const { return bHasSubmittedWindow; }
	FIntPoint GetSubmittedWindowMinCell() const { return SubmittedWindowMinCell; }
	int32 GetResolution() const { return Resolution; }
	int32 GetMode() const { return Mode; }
	float GetStepsPerSecond() const { return StepsPerSecond; }
	uint32 GetSubmittedStepCount() const { return SubmittedStepCount; }
	int32 GetSeenRejectedStepCount() const { return SeenRejectedStepCount; }
	int32 ReadRejectedStepCount() const;
	int32 GetLastDroppedSphereCount() const { return LastDroppedSphereCount; }
	int32 GetLastSkippedObstacleCount() const { return LastSkippedObstacleCount; }
	FTDFluidGpuFog3DFrameInput BuildFrameInput(const FTDFog3DStepRequest& Request) const;
	FIntPoint PreviewNextWindowMinCell(const FVector& PlayerWorld) const;

	static FIntPoint ComputeWorldCell(const FVector2D& WorldXY, double CellSizeCm);
	static FIntPoint SnapWindowMinCell(const FIntPoint& PlayerCell, int32 WindowCells);
	static FIntPoint FollowWindowMinCell(const FIntPoint& CurrentWindowMinCell, const FIntPoint& PlayerCell, int32 WindowCells, int32 MarginCells);
	static bool IsWindowJumpTooFar(const FIntPoint& PreviousWindowMinCell, const FIntPoint& NewWindowMinCell, int32 WindowCells);
	static FTDFog3DClockDecision DecideClock(double StepCreditSeconds, double SecondsSinceSubmittedStep, float StepsPerSecond, float MaximumStepSeconds);
	static bool IsUsableOutputTarget(const UTextureRenderTargetVolume& OutputTarget, int32 Resolution);
	static int32 ResolveMode(int32 ConsoleMode, int32 DataMode);
	static int32 ResolveQualityLevel(int32 ConsoleLevel, int32 DataLevel);
	static int32 ComputeBladeSphereCount(double BladeLengthCm, double RadiusCm);
	static void ComputeBladeSphereCenters(const FVector& BaseWorld, const FVector& TipWorld, double RadiusCm, TArray<FVector>& OutCenters);
	static void PackSpheres(
		const TArray<FTDFog3DInputSample>& Inputs,
		const TMap<int32, FVector>& PreviousPositionsBySlot,
		const FVector2D& WindowOriginWorldXY,
		double FloorZ,
		bool bIsResetStep,
		float ElapsedSeconds,
		bool bAppliesWindGain,
		float MaximumSpeedCmPerSec,
		int32 MaximumSphereCount,
		TArray<FTDFluidGpuFog3DSphere>& OutSpheres,
		int32& OutDroppedCount);
	static void PackSpheres(
		const TArray<FTDFog3DInputSample>& Inputs,
		const TMap<int32, FVector>& PreviousPositionsBySlot,
		const FVector2D& WindowOriginWorldXY,
		double FloorZ,
		bool bIsResetStep,
		float ElapsedSeconds,
		bool bAppliesWindGain,
		float MaximumSpeedCmPerSec,
		int32 MaximumSphereCount,
		double CellSizeCm,
		int32 Resolution,
		TArray<FTDFluidGpuFog3DSphere>& OutSpheres,
		TArray<FTDFluidGpuFog3DSphereAnchor>& OutSphereAnchors,
		int32& OutDroppedCount);
	static FTDFluidGpuFog3DSphereAnchor PackSphereAnchor(const FTDFog3DInputSample& Input, const FVector& WindowOffset, double CellSizeCm, int32 Resolution);
	static bool ReadUprightYaw(const FTDFluidSdfObstacle& Obstacle, double& OutYawDegrees);
	static void SelectAndPackObstacles(
		const TArray<FTDFluidSdfObstacle>& Registered,
		const FVector2D& WindowOriginWorldXY,
		const FVector2D& WindowCenterWorldXY,
		double FloorZ,
		const FTDFluidGpuFog3DInflation& Inflation,
		int32 MaximumObstacleCount,
		TArray<FTDFluidGpuFog3DObstacle>& OutObstacles,
		TArray<FTDFog3DObstacleKey>& OutKeys,
		int32& OutSkippedCount);
	static bool HaveObstaclesChanged(const TArray<FTDFog3DObstacleKey>& PreviousKeys, const TArray<FTDFog3DObstacleKey>& NewKeys);
	static bool ShouldResetState(const FTDFog3DResetInputs& Inputs);
	static FVector4f ComputeMaterialWindow(const FIntPoint& SubmittedWindowMinCell, double CellSizeCm, const FVector& ZoneReferenceWorld, double FloorZ, int32 WindowCells);

private:
	friend struct FTDFog3DLayerTestAccess;
	friend struct FTDFog3DLayerRhiTestAccess;

	struct FPackedStep
	{
		FTDFluidGpuFog3DFrameInput FrameInput;
		TArray<FTDFog3DObstacleKey> ObstacleKeys;
		int32 DroppedSphereCount = 0;
		int32 SkippedObstacleCount = 0;
	};

	FIntPoint ComputeNextWindowMinCell(const FVector& PlayerWorld) const;
	void EnsureRenderSlot();
	FPackedStep PackStep(const FTDFog3DStepRequest& Request, int32 RejectedStepCount) const;
	FPackedStep PackAndCommitStep(const FTDFog3DStepRequest& Request);
	void CommitStep(const FTDFog3DStepRequest& Request, const FPackedStep& Packed, int32 RejectedStepCount);
	void ConsumeStepClock();

	FTDFog3DSettings Settings;
	int32 QualityLevel = -1;
	int32 Mode = 0;
	int32 Resolution = 0;
	float StepsPerSecond = 0.0f;
	double SecondsSinceSubmittedStep = 0.0;
	double StepCreditSeconds = 0.0;
	bool bHasSubmittedWindow = false;
	FIntPoint SubmittedWindowMinCell = FIntPoint::ZeroValue;
	bool bZoneReentryPending = false;
	bool bVolumetricFogReenablePending = false;
	bool bConfigurationChangePending = false;
	TMap<int32, FVector> PreviousPositionsBySlot;
	TArray<FTDFog3DObstacleKey> PreviousObstacleKeys;
	uint32 SubmittedStepCount = 0;
	int32 SeenRejectedStepCount = 0;
	int32 LastDroppedSphereCount = 0;
	int32 LastSkippedObstacleCount = 0;
	TSharedPtr<FTDFog3DRenderSlot, ESPMode::ThreadSafe> RenderSlot;
	TSharedPtr<FTDFog3DGroundReadbackJob, ESPMode::ThreadSafe> GroundReadbackJob;
};
