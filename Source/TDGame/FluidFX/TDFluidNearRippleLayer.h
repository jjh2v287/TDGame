#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"

class FTDFluidGpuNearRippleState;
class UTexture;
class UTextureRenderTarget2D;

struct FTDFluidNearRippleMaskInputs
{
	UTexture* WaterSurfaceTexture = nullptr;
	UTexture* AuxTexture = nullptr;
	FVector2D MainWindowCenterCm = FVector2D::ZeroVector;
	FVector2D MainWindowExtentCm = FVector2D::ZeroVector;
	float WaterDepthReferenceCm = 300.0f;
	float ObstacleDistanceOutputRangeCm = 400.0f;
	bool bHasObstacleDistanceAux = false;
};

struct FTDFluidNearRippleSubstepPlan
{
	int32 SubstepCount = 1;
	float SubstepDeltaSeconds = 0.0f;
};

struct FTDFluidNearRippleRainInput
{
	float DropsPerSquareMeterPerSec = 0.0f;
	float DropImpulseCm = 0.0f;
	float DropRadiusCm = 0.0f;
	uint32 RandomSeed = 0;
};

struct FTDFluidNearRippleRainFrame
{
	float DropProbability = 0.0f;
	float DropImpulseCm = 0.0f;
	float DropRadiusCells = 1.0f;
	int32 BlockSizeCells = 2;
	uint32 Seed = 0;
};

struct FTDFluidNearRippleSourceSelection
{
	FVector2D WorldXY = FVector2D::ZeroVector;
	float RadiusCm = 0.0f;
	float ImpulseCmPerSec = 0.0f;
	float IdleWeight = 0.0f;
};

class TDGAME_API FTDFluidNearRippleLayer
{
public:
	FTDFluidNearRippleLayer() = default;
	~FTDFluidNearRippleLayer();
	FTDFluidNearRippleLayer(const FTDFluidNearRippleLayer&) = delete;
	FTDFluidNearRippleLayer& operator=(const FTDFluidNearRippleLayer&) = delete;

	void Configure(const FTDFluidWaterNearRippleSettings& InSettings);
	void AddMovingSource(const FVector2D& WorldXY, const FVector2D& VelocityXY, float StampRadiusCm);
	void AddIdleContact(const FVector2D& WorldXY);
	void ClearPendingSources();
	void SetRainInput(const FTDFluidNearRippleRainInput& InRainInput);
	void SubmitStep(const FTDFluidNearRippleMaskInputs& Masks, const FVector2D& CenterWorldXY, float DeltaTime, UTextureRenderTarget2D& OutputTarget);
	void Release();

	int32 GetResolution() const;
	float GetExtentCm() const;
	FVector2D GetSnappedCenterWorld() const;
	bool NeedsReset() const { return bNeedsReset; }
	TArray<FTDFluidNearRippleSourceSelection> SelectFrameSources(const FVector2D& WindowCenterWorld) const;

	static int32 SanitizeResolution(int32 RequestedResolution);
	static FIntPoint ComputeWindowMinCell(const FVector2D& CenterWorldXY, double CellSizeCm, int32 Resolution);
	static FTDFluidNearRippleSubstepPlan ComputeSubstepPlan(float WaveSpeedCmPerSec, float DeltaTime, float CellSizeCm, int32 MaxSubsteps);
	static double ComputeIdleContactPhase(double LayerTimeSeconds, float ContactHz);
	static bool IsCellInsideWindow(const FIntPoint& WorldCell, const FIntPoint& WindowMinCell, int32 Resolution);
	static FIntPoint ComputeStorageCell(const FIntPoint& WorldCell, int32 Resolution);
	static bool ShouldResetForShift(const FIntPoint& PreviousWindowMinCell, const FIntPoint& WindowMinCell, int32 Resolution);
	static bool ResolveEnabled(int32 ConsoleState, bool bPresetEnabled);
	static FTDFluidNearRippleRainFrame MakeRainFrame(const FTDFluidNearRippleRainInput& Input, float CellSizeCm, float SubstepDeltaSeconds);
	static FIntPoint ComputeRainBlock(const FIntPoint& WorldCell, int32 BlockSizeCells);
	static bool FindRainDropInBlock(const FIntPoint& Block, uint32 SubstepIndex, const FTDFluidNearRippleRainFrame& Frame, FVector2f& OutDropOffsetCells);
	static float ComputeRainDropWeight(const FIntPoint& WorldCell, uint32 SubstepIndex, const FTDFluidNearRippleRainFrame& Frame);

private:
	struct FPendingSource
	{
		FVector2D WorldXY = FVector2D::ZeroVector;
		float StampRadiusCm = 0.0f;
		float SpeedCmPerSec = 0.0f;
		bool bIsIdleContact = false;
	};

	double ComputeCellSizeCm() const;

	FTDFluidWaterNearRippleSettings Settings;
	TArray<FPendingSource> PendingSources;
	FTDFluidNearRippleRainInput RainInput;
	uint32 RainSubstepCounter = 0;
	TSharedPtr<FTDFluidGpuNearRippleState, ESPMode::ThreadSafe> RenderState;
	FIntPoint CurrentWindowMinCell = FIntPoint::ZeroValue;
	double LayerTimeSeconds = 0.0;
	bool bNeedsReset = true;
	bool bHasWindow = false;
};
