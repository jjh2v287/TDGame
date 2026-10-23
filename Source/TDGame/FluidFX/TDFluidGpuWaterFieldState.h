#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterInputRaster.h"
#include "TDFluidGpuInputs.h"

class FTDFluidGpuWaterFieldState
{
public:
	void Initialize(const FTDFluidSimulationConfig& Config, int32 SimulationResolution, int32 InFieldDivisor, const FVector2D& InCenterPlaneCm, const FVector2D& InExtentCm);
	void UpdateConfig(const FTDFluidSimulationConfig& Config);
	void Release();
	bool IsInitialized() const;
	bool IsValidFieldCell(int32 FieldX, int32 FieldY) const;
	void SetFlowGuidesAndRegions(const TArray<FTDFluidFlowGuide>& Guides, const TArray<FTDFluidWaterRegion>& Regions);
	void ShiftWindow(const FIntPoint& ShiftSimulationCells, const FVector2D& NewCenterPlaneCm);
	void SetTerrainElevationAtFieldCell(int32 FieldX, int32 FieldY, float ElevationCm);
	void RequestFullUpload(int32 GatherCount);
	void RasterizeDirtyCells();
	void MoveUpdatesInto(FTDFluidGpuActorInput& OutActorInput);
	int32 GetFieldResolution() const { return FieldResolution; }
	int32 GetFieldDivisor() const { return FieldDivisor; }
	FIntPoint GetFieldOrigin() const { return FieldOrigin; }
	FVector2D FieldCellToPlaneCenter(int32 FieldX, int32 FieldY) const;
	float GetSurfaceHeightAtFieldCell(int32 FieldX, int32 FieldY) const;

private:
	void ShiftDirtyFlags(TArray<uint8>& Flags, int32 ShiftCells) const;
	void ShiftPendingUpdates(const FIntPoint& FieldShift);
	void QueueTerrainResetForNewCells(const FIntPoint& FieldShift);
	void AppendFullGuideUpload(TArray<FTDFluidGpuWaterGuideCellUpdate>& OutUpdates) const;
	void AppendFullTerrainUpload(TArray<FTDFluidGpuTerrainCellUpdate>& OutUpdates) const;

	FTDFluidWaterInputRaster Raster;
	FVector2D CenterPlaneCm = FVector2D::ZeroVector;
	FVector2D ExtentCm = FVector2D::ZeroVector;
	float BankFalloffCm = 0.0f;
	float BankSpeedRatio = 0.0f;
	int32 FieldResolution = 0;
	int32 FieldDivisor = 1;
	FIntPoint FieldOrigin = FIntPoint::ZeroValue;
	TArray<float> SurfaceHeight;
	TArray<float> TargetVelocityX;
	TArray<float> TargetVelocityY;
	TArray<float> GuideWeight;
	TArray<float> GuideDescent;
	TArray<float> TerrainElevation;
	TArray<uint8> TerrainKnown;
	TArray<uint8> DirtyColumns;
	TArray<uint8> DirtyRows;
	TArray<FTDFluidGpuWaterGuideCellUpdate> PendingGuideUpdates;
	TArray<FTDFluidGpuTerrainCellUpdate> PendingTerrainUpdates;
	int32 FullUploadGathersRemaining = 0;
	bool bRasterFullDirty = true;
	bool bHasDirtyStrips = false;
};
