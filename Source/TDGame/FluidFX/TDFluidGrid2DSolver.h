#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"
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

	bool ShiftOriginForSlidingWindow(const FVector2D& DesiredCenterWorld);
	void SetUniformTerrainSlope(const FVector2D& InSlopeGradient);
	void SetPlaneUpDirection(const FVector2D& InPlaneUpDirection);
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

private:
	int32 ToCellIndex(int32 CellX, int32 CellY) const { return CellY * Resolution + CellX; }
	bool IsValidCell(int32 CellX, int32 CellY) const { return CellX >= 0 && CellX < Resolution && CellY >= 0 && CellY < Resolution; }
	bool IsSolidCell(int32 CellX, int32 CellY) const { return !IsValidCell(CellX, CellY) || SolidCellMask.GetData()[ToCellIndex(CellX, CellY)] != 0; }
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

	FTDFluidSimulationConfig Config;
	int32 Resolution = 0;
	int32 ReinitializationGeneration = 0;
	FVector2D CenterWorld = FVector2D::ZeroVector;
	double SimulationTimeSeconds = 0.0;
	FVector2D PlaneUpDirection = FVector2D::ZeroVector;

	TArray<float> PrimaryDensity;
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
};
