#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterInputRaster.h"
#include "Math/Float16Color.h"

class TDGAME_API FTDFluidGrid2DSolver
{
public:
	void Initialize(const FTDFluidSimulationConfig& InConfig, const FVector2D& InitialCenterWorld);
	void UpdateConfig(const FTDFluidSimulationConfig& InConfig);
	void ResetSimulation();

	bool IsInitialized() const { return Resolution > 0 && PrimaryDensity.Num() == Resolution * Resolution; }
	int32 GetResolution() const { return Resolution; }
	int32 GetReinitializationGeneration() const { return ReinitializationGeneration; }
	FVector2D GetCenterWorld() const { return CenterWorld; }
	FVector2D GetExtentCm() const { return Config.SimExtentCm; }
	const FTDFluidSimulationConfig& GetConfig() const { return Config; }
	bool IsAreaWrapped() const { return Config.AreaAddressMode == ETDFluidAreaAddressMode::Wrap; }

	bool ShiftOriginForSlidingWindow(const FVector2D& DesiredCenterWorld);
	void SetUniformTerrainSlope(const FVector2D& InSlopeGradient);
	void SetPlaneUpDirection(const FVector2D& InPlaneUpDirection);
	void SetAdvectionOffsetPlane(const FVector2D& InAdvectionOffsetPlaneCmPerSec);
	void SetNoiseLatticeOffsetCells(const FIntPoint& InNoiseLatticeOffsetCells);
	bool MoveWindowWithoutShift(const FVector2D& NewCenterWorld);
	void SetTerrainElevationAtCell(int32 CellX, int32 CellY, float ElevationCm);
	void ComputeTerrainSlopeFromElevation();

	void RasterizeSdfObstacles(const TArray<FTDFluidSdfObstacle>& Obstacles);
	void ApplyBrushStamps(const TArray<FTDFluidBrushStamp>& Stamps, float DeltaTime);
	void StepSimulation(float DeltaTime);
	void ComputeFlowNoiseAccelerationField(TArray<FVector2D>& OutAccelerationCmPerSec2, bool bForceSingleThread = false) const;

	bool WorldToGridUV(const FVector2D& WorldPos, FVector2D& OutUV) const;
	FVector2D CellToWorldCenter(int32 CellX, int32 CellY) const;
	bool SampleAtWorldPosition(const FVector2D& WorldPos, FTDFluidCellSample& OutSample) const;
	FTDFluidCellSample SampleAtCell(int32 CellX, int32 CellY) const;

	void PackOutputPixels(TArray<FFloat16Color>& OutDensityPressurePixels, TArray<FFloat16Color>& OutVelocityPixels) const;
	uint32 ComputeStateChecksum() const;
	void PackAuxPixels(TArray<FFloat16Color>& OutAuxPixels) const;
	void PackObstacleDistanceAuxPixels(TArray<FFloat16Color>& OutAuxPixels) const;
	int32 GetObstacleSdfRawGeneration() const { return ObstacleSdfRawGeneration; }
	static constexpr float ObstacleDistanceEmptySdfCm = 10000.0f;
	bool IsHeatChannelActive() const;
	bool UsesDenseDensity() const;
	bool UsesFlowNoiseSpeedMask() const;
	float GetStampDensityCeiling() const;
	static float ComputeFlowNoiseSpeedMask(float SpeedCmPerSec, float InverseReferenceCmPerSec);

	static constexpr float NoWaterSurfaceHeight = -TNumericLimits<float>::Max();
	static float GetOutputSpeedReferenceCmPerSec();
	static bool IsWaterWetnessConsoleEnabled();
	double GetSimulationTimeSeconds() const { return SimulationTimeSeconds; }
	void SetFlowGuides(const TArray<FTDFluidFlowGuide>& Guides);
	void SetWaterRegions(const TArray<FTDFluidWaterRegion>& Regions);
	void PackWaterSurfacePixels(TArray<FFloat16Color>& OutPixels, float TerrainReferenceZCm) const;
	uint32 ComputeWaterStateChecksum() const;
	float GetWaterSurfaceHeightAtCell(int32 CellX, int32 CellY) const;
	bool IsTerrainSamplingCompleteForWater() const;

private:
	int32 ToCellIndex(int32 CellX, int32 CellY) const { return CellY * Resolution + CellX; }
	bool IsValidCell(int32 CellX, int32 CellY) const { return CellX >= 0 && CellX < Resolution && CellY >= 0 && CellY < Resolution; }
	bool IsSolidCell(int32 CellX, int32 CellY) const { return !IsValidCell(CellX, CellY) || SolidCellMask.GetData()[ToCellIndex(CellX, CellY)] != 0; }
	int32 WrapCoordinate(int32 Coordinate) const { return (Coordinate + Resolution) % Resolution; }
	void ResolveWrappedNeighborIndices(int32 CellX, int32 CellY, int32& OutRightIndex, int32& OutLeftIndex, int32& OutUpIndex, int32& OutDownIndex) const;
	void ApplyVorticityConfinementWrapped(float DeltaTime, float SpacingX, float SpacingY);
	void RefreshObstacleCellCache();
	void BuildBorderFadeTable(float* OutFadeByCoordinate) const;
	void BuildBorderDampingTable(const float* FadeByCoordinate, float DeltaTime, float* OutDampingByCoordinate) const;
	float SampleBilinear(const TArray<float>& Field, float GridX, float GridY) const;
	void ComputeRelativeCellSpacing(float& OutSpacingX, float& OutSpacingY) const;

	void ApplyBrushStamp(const FTDFluidBrushStamp& Stamp, float DeltaTime, float VelocityBlend);
	void StepSimplePainter(float DeltaTime);
	void StepFluidSim(float DeltaTime);
	void RefillAmbientDensity(float DeltaTime);
	void RefillAmbientDensityWithNoise(float RefillBlend);
	void ApplyPendingRadialPush();
	void RestoreVelocityBeforeRadialPush();
	void ApplyTerrainSlopeAcceleration(float DeltaTime);
	void ApplyFlowNoiseAcceleration(float DeltaTime);
	void ApplyBuoyancyAcceleration(float DeltaTime);
	void ApplyDirectionalOutflow(float DeltaTime);
	void EnforceObstacleBoundaries();
	void ApplyVorticityConfinement(float DeltaTime);
	void AdvectVelocity(float DeltaTime);
	void SolvePressureProjection();
	void ComputeVelocityDivergence(float SpacingX, float SpacingY);
	void RelaxPressureJacobi(float InvSpacingXSquared, float InvSpacingYSquared, float InvNeighborWeightSum);
	void RemoveMeanPressure();
	void SubtractPressureGradient(float SpacingX, float SpacingY, float PressureDisplayScale);
	void AdvectDensities(float DeltaTime);
	void ClampVelocityMagnitudes();
	void BuildBorderAlphaTable(float* OutAlphaByCoordinate) const;
	void SynchronizeHeatBuffersWithConfig();
	void ApplyHeatStamp(const FTDFluidBrushStamp& Stamp, float DeltaTime);
	void ApplyHeatBuoyancyAcceleration(float DeltaTime);
	void ApplyVortexAcceleration(float DeltaTime);
	void AccumulateVortexRadialDrift();
	void EnforceObstacleBoundariesWithReflection();
	void ApplyEdgeReflection();
	void AdvectHeat(float DeltaTime);
	void ApplyDirectionalOutflowToHeat(float DeltaTime);

	bool ShouldAllocateWaterBuffers() const;
	bool IsWaterLayerActive() const;
	void ResetWaterBuffers();
	void ReleaseWaterBuffers();
	void SynchronizeWaterBuffersWithConfig(const FTDFluidSimulationConfig& PreviousConfig);
	void ShiftWaterBuffers(int32 ShiftCellsX, int32 ShiftCellsY);
	void CommitWaterObstacleSdf();
	void ComposeWaterObstacleSdf();
	float ComputeWaterDepthAtIndex(int32 CellIndex) const;
	void StepFluidSimWater(float DeltaTime);
	void UpdateWetnessIfEnabled(float DeltaTime);
	void UpdateWetness(float DeltaTime);
	float ComputeWetnessSourceAtCell(int32 CellX, int32 CellY, float SwashReachCm, float InvBankWetHeightCm) const;
	void PrepareWaterInputs();
	void RasterizeWaterInputs();
	void RefreshWaterDrySdf();
	void ApplyTerrainSlopeAccelerationDryOnly(float DeltaTime);
	void ApplyFlowGuideAcceleration(float DeltaTime);
	void AdvectVelocityWater(float DeltaTime);
	void SolvePressureProjectionWater();
	bool HasOpenWaterBorderCell() const;
	void ComputeVelocityDivergenceOpenBorders(float SpacingX, float SpacingY);
	void RelaxPressureJacobiOpenBorders(float InvSpacingXSquared, float InvSpacingYSquared, float InvNeighborWeightSum);
	void SubtractPressureGradientOpenBorders(float SpacingX, float SpacingY, float PressureDisplayScale);
	void AddWhitewaterSources(float DeltaTime);
	void StepRipples(float DeltaTime);
	void AdvectRipples(float DeltaTime);

	FTDFluidSimulationConfig Config;
	int32 Resolution = 0;
	int32 ReinitializationGeneration = 0;
	FVector2D CenterWorld = FVector2D::ZeroVector;
	double SimulationTimeSeconds = 0.0;
	FVector2D PlaneUpDirection = FVector2D::ZeroVector;

	TArray<float> PrimaryDensity;
	TArray<float> Heat;
	TArray<float> ScratchHeat;
	TArray<float> SelectiveDensity;
	TArray<float> SurfaceDisplacement;
	TArray<float> ScratchDensity;
	TArray<float> ScratchSelectiveDensity;
	TArray<float> ScratchSurfaceDisplacement;
	TArray<float> VelocityX;
	TArray<float> VelocityY;
	TArray<float> ScratchVelocityX;
	TArray<float> ScratchVelocityY;
	TArray<float> PendingRadialPushX;
	TArray<float> PendingRadialPushY;
	TArray<float> Pressure;
	TArray<float> PressureSolution;
	TArray<float> ScratchPressure;
	TArray<float> Divergence;
	TArray<float> VorticityCurl;
	TArray<float> ObstacleSdf;
	TArray<float> TerrainElevation;
	TArray<FVector2D> TerrainSlope;

	TArray<uint8> SolidCellMask;
	TArray<uint8> SolidFreeStencilRows;
	float ObstacleSdfMinimum = 0.0f;
	bool bIsTerrainSlopeZero = true;
	bool bHasPendingRadialPush = false;
	FVector2D AdvectionOffsetPlaneCmPerSec = FVector2D::ZeroVector;
	FIntPoint NoiseLatticeOffsetCells = FIntPoint::ZeroValue;
	TArray<FVector4f> KernelPartialSums;
	bool UsesKernelPressure() const;
	void RelaxPressureKernelFeedback();
	void ApplyFlowAmplitudeNoiseDamping(float DeltaTime);
	void AccumulateAdvectionOffset();

	TArray<float> WaterSurfaceHeight;
	TArray<float> WaterDrySdf;
	TArray<float> ObstacleSdfRaw;
	int32 ObstacleSdfRawGeneration = 0;
	TArray<uint8> TerrainKnownMask;
	TArray<float> FlowTargetVelocityX;
	TArray<float> FlowTargetVelocityY;
	TArray<float> FlowGuideWeight;
	TArray<float> FlowGuideDescent;
	TArray<float> Wetness;
	TArray<float> RippleHeight;
	TArray<float> RippleHeightVelocity;
	TArray<float> ScratchRippleHeight;
	TArray<float> ScratchRippleHeightVelocity;
	TArray<uint8> WaterDirtyColumns;
	TArray<uint8> WaterDirtyRows;
	FTDFluidWaterInputRaster WaterInputRaster;
	int32 TerrainKnownCellCount = 0;
	bool bWaterRasterFullDirty = true;
	bool bWaterHasDirtyStrips = false;
	bool bWaterDryDirty = true;
	bool bWaterVelocityInitPending = true;
	bool bHasRippleActivity = false;
	bool bIsWetnessUpdating = false;
};
