#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidGpuWaterFieldState.h"
#include "FluidFX/TDFluidTypes.h"
#include "HAL/CriticalSection.h"
#include "TDFluidGpuBackendSelection.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuStepPasses.h"

class FRHICommandListImmediate;
class FTDFluidGrid2DSolver;
class FTextureRenderTargetResource;

struct FTDFluidReadbackField
{
	FVector2D WindowCenterPlaneCm = FVector2D::ZeroVector;
	FVector2D ExtentCm = FVector2D::ZeroVector;
	uint32 FrameNumber = 0;
	TArray<float> PrimaryDensity;
	TArray<float> SelectiveDensity;
	TArray<FVector2f> VelocityCmPerSec;

	static int32 GetResolution() { return TDFluidGpuSolverConstants::ReadbackTileResolution; }

	bool IsValid() const;
	void Reset();
	bool CopyFromAtlasTile(
		TConstArrayView<FFloat16Color> AtlasTexels,
		const FIntPoint& AtlasSize,
		const FIntPoint& TileOrigin,
		uint32 InFrameNumber,
		const FVector2D& InWindowCenterPlaneCm,
		const FVector2D& InExtentCm);
	void DownsampleFromSolver(const FTDFluidGrid2DSolver& Solver);
	bool TryMapPlanePointToFieldCell(const FVector2D& PlanePoint, FIntPoint& OutFieldCell) const;
	FTDFluidCellSample SampleFieldCell(int32 FieldX, int32 FieldY) const;
	bool SampleBilinear(const FVector2D& PlanePoint, FTDFluidCellSample& OutSample) const;
	bool ComputeDensityCentroid(bool bUseSelectiveDensity, FVector2D& OutCentroid, float& OutAreaCm2) const;

private:
	void AllocateCells(const FVector2D& InWindowCenterPlaneCm, const FVector2D& InExtentCm, uint32 InFrameNumber);
};

class FTDFluidGpuSolverProxy
{
public:
	static ETDFluidGpuSolverPreference ToSolverPreference(ETDFluidSolverBackend SolverBackend);
	static FTDFluidGpuSimulationSettings MakeSimulationSettings(const FTDFluidSimulationConfig& Config, int32 InResolution);
	static FTDFluidGpuStampInput MakeStampInput(const FTDFluidBrushStamp& PlaneStamp, bool bCarriesRippleImpulse = false);
	static FTDFluidGpuObstacleInput MakeObstacleInput(const FTDFluidSdfObstacle& PlaneObstacle);
	static int64 EstimateActorMemoryBytes(ETDFluidSimMode SimMode, int32 RequestedResolution, bool bUsesHeatChannel = false, bool bUsesTransmittance = false, ETDFluidGpuPressureSolverMode PressureSolverMode = ETDFluidGpuPressureSolverMode::Auto);

	void Initialize(const FTDFluidSimulationConfig& Config, int32 InResolution, const FVector2D& InitialCenterPlaneCm);
	void UpdateSettings(const FTDFluidSimulationConfig& Config);
	void ResetState();

	bool IsInitialized() const { return Resolution > 0; }
	bool WasLastSettingsUpdateReset() const { return bWasLastSettingsUpdateReset; }
	int32 GetResolution() const { return Resolution; }
	FVector2D GetExtentCm() const { return Settings.ExtentCm; }
	FVector2D GetCenterPlaneCm() const { return CenterPlaneCm; }
	int32 GetGeneration() const { return Generation; }
	double GetSimulationTimeSeconds() const { return SimulationTimeSeconds; }
	bool UsesVelocityOutput() const { return Settings.SimMode == ETDFluidGpuSimMode::FluidSim; }
	const FTDFluidGpuSimulationSettings& GetSettings() const { return Settings; }
	const FTDFluidReadbackField& GetReadbackField() const { return ReadbackField; }

	FVector2D CellToPlaneCenter(int32 CellX, int32 CellY) const;
	bool PlanePointToGridUV(const FVector2D& PlanePoint, FVector2D& OutUV) const;
	FTDFluidCellSample SampleSimulationCell(int32 CellX, int32 CellY) const;
	FIntPoint ShiftWindowTowards(const FVector2D& DesiredCenterPlaneCm);
	void SetObstacles(TConstArrayView<FTDFluidSdfObstacle> PlaneObstacles);
	void SetTerrainElevationAtCell(int32 CellX, int32 CellY, float ElevationCm);
	void MarkTerrainSamplingComplete() { bIsTerrainElevationActive = true; }

	bool UsesWaterLayer() const { return Settings.bUsesWaterLayer; }
	int32 GetWaterFieldResolution() const;
	FVector2D WaterFieldCellToPlaneCenter(int32 FieldX, int32 FieldY) const;
	float GetWaterSurfaceHeightAtFieldCell(int32 FieldX, int32 FieldY) const;
	void SetWaterInputs(const TArray<FTDFluidFlowGuide>& Guides, const TArray<FTDFluidWaterRegion>& Regions);
	void SetWaterTerrainReferenceZCm(float InTerrainReferenceZCm) { WaterTerrainReferenceZCm = InTerrainReferenceZCm; }
	void SetWaterProbePlanePoints(TConstArrayView<FVector2D> PlanePoints, int32 InProbeGeneration);
	bool TryGetWaterProbeSamples(TArray<FTDFluidGpuWaterProbeSample>& OutSamples, uint32& OutFrameNumber) const;

	void PrepareStep(float DeltaTime, TConstArrayView<FTDFluidBrushStamp> PlaneStamps, const FVector2D& PlaneUpDirection, bool bAllowsTerrainElevation);
	bool HasPreparedStep() const { return bHasPreparedStep; }
	void MarkObstacleDistanceAuxStale() { bNeedsObstacleDistanceAuxRefresh = true; }
	bool GatherStep(
		uint32 FrameNumber,
		int32 TileIndex,
		FTextureRenderTargetResource* DensityPressureOutput,
		FTextureRenderTargetResource* VelocityOutput,
		FTDFluidGpuActorInput& OutActorInput,
		FTextureRenderTargetResource* AuxOutput = nullptr,
		FTextureRenderTargetResource* TransmittanceOutput = nullptr,
		FTextureRenderTargetResource* WaterSurfaceOutput = nullptr,
		int32 WaterProbeFirstSlot = 0);
	void SetTransmittanceInput(const FTDFluidGpuTransmittanceInput& Input) { PendingTransmittanceInput = Input; }
	bool ConsumeReadbackFrame(const FTDFluidGpuReadbackFrame& ReadbackFrame);

	bool HasActorKey() const { return ActorKey != 0; }
	uint64 GetActorKey() const { return ActorKey; }
	void AssignActorKey(uint64 InActorKey);
	uint64 ReleaseActorKey();

private:
	struct FTDFluidGpuSubmissionRecord
	{
		uint32 FrameNumber = 0;
		int32 TileIndex = INDEX_NONE;
		FVector2D WindowCenterPlaneCm = FVector2D::ZeroVector;
		FVector2D ExtentCm = FVector2D::ZeroVector;
		int32 WaterProbeFirstSlot = 0;
		int32 WaterProbeCount = 0;
		int32 WaterProbeGeneration = 0;
	};

	bool IsValidCell(int32 CellX, int32 CellY) const;
	void ShiftPendingTerrainUpdates(const FIntPoint& ShiftCells);
	void RecordSubmission(uint32 FrameNumber, int32 TileIndex, int32 WaterProbeFirstSlot, int32 WaterProbeCount);
	int32 GatherWaterStep(FTDFluidGpuActorInput& OutActorInput, FTextureRenderTargetResource* WaterSurfaceOutput, int32 WaterProbeFirstSlot);
	void ConsumeWaterProbeSamples(const FTDFluidGpuReadbackFrame& ReadbackFrame, const FTDFluidGpuSubmissionRecord& Submission);
	void AdvanceSimulationTime(float DeltaTime);

	FTDFluidGpuSimulationSettings Settings;
	FTDFluidGpuTransmittanceInput PendingTransmittanceInput;
	FTDFluidReadbackField ReadbackField;
	TArray<FTDFluidGpuStampInput> PreparedStamps;
	TArray<FTDFluidGpuObstacleInput> Obstacles;
	TArray<FTDFluidGpuTerrainCellUpdate> PendingTerrainUpdates;
	TArray<FTDFluidGpuSubmissionRecord> RecentSubmissions;
	FVector2D CenterPlaneCm = FVector2D::ZeroVector;
	FVector2D PreparedPlaneUpDirection = FVector2D::ZeroVector;
	FIntPoint PendingShiftCells = FIntPoint::ZeroValue;
	double SimulationTimeSeconds = 0.0;
	uint64 ActorKey = 0;
	int32 Resolution = 0;
	int32 Generation = 0;
	float PreparedDeltaTimeSeconds = 0.0f;
	bool bNeedsReset = false;
	bool bHasObstacleListChanged = false;
	bool bNeedsObstacleDistanceAuxRefresh = true;
	bool bIsTerrainElevationActive = false;
	bool bPreparedAllowsTerrainElevation = false;
	bool bHasPreparedStep = false;
	FTDFluidGpuWaterFieldState WaterFieldState;
	FTDFluidSimulationConfig WaterFieldConfig;
	float WaterTerrainReferenceZCm = 0.0f;
	TArray<FVector2D> WaterProbePlanePoints;
	int32 WaterProbeGeneration = 0;
	TArray<FTDFluidGpuWaterProbeSample> LatestWaterProbeSamples;
	uint32 LatestWaterProbeFrameNumber = 0;
	int32 LatestWaterProbeGeneration = -1;
	FTDFluidGpuBatchKey LastGatheredBatchKey;
	bool bHasGatheredBatchKey = false;
	bool bWasUpdatingWaterWetness = false;
	bool bWasLastSettingsUpdateReset = false;
};

class FTDFluidGpuSolverRenderState
{
public:
	void ExecuteFrame(FRHICommandListImmediate& RHICmdList, const FTDFluidGpuFrameInputs& FrameInputs);
	bool TakeLatestReadback(FTDFluidGpuReadbackFrame& OutReadbackFrame);

private:
	void PublishReadyReadback();

	TArray<FTDFluidGpuBatch> Batches;
	FTDFluidGpuReadbackRing ReadbackRing;
	FCriticalSection ReadbackMutex;
	FTDFluidGpuReadbackFrame LatestReadback;
	bool bHasUnreadReadback = false;
};
