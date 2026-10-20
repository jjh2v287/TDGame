#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumClassFlags.h"
#include "PooledRenderTarget.h"
#include "TDFluidGpuInputs.h"
#include "TDFluidGpuStampBins.h"

enum class ETDFluidGpuActorFlags : uint32
{
	None = 0,
	IsSimplePainter = 1u << 0,
	NeedsReset = 1u << 1,
	HasShift = 1u << 2,
	RasterizesObstacles = 1u << 3,
	HasObstacles = 1u << 4,
	HasTerrainElevation = 1u << 5,
	UsesTerrainSlopeAcceleration = 1u << 6,
	UsesFlowNoise = 1u << 7,
	UsesBuoyancy = 1u << 8,
	UsesVorticity = 1u << 9,
	UsesOutflow = 1u << 10,
	UsesAmbientRefill = 1u << 11,
	UsesAmbientNoise = 1u << 12,
	HasStamps = 1u << 13,
	UsesHeatChannel = 1u << 14,
	UsesBoundaryReflection = 1u << 15,
	UsesVortexForce = 1u << 16,
	WritesAuxOutput = 1u << 17
};
ENUM_CLASS_FLAGS(ETDFluidGpuActorFlags);

struct alignas(16) FTDFluidGpuActorParameters
{
	int32 SliceIndex = 0;
	int32 Resolution = 0;
	uint32 Flags = 0;
	uint32 PressureIterations = 0;

	FVector2f ExtentCm = FVector2f::ZeroVector;
	FVector2f CellSizeCm = FVector2f::ZeroVector;

	FVector2f RelativeCellSpacing = FVector2f::ZeroVector;
	float InvSpacingXSquared = 0.0f;
	float InvSpacingYSquared = 0.0f;

	float InvNeighborWeightSum = 0.0f;
	float PressureDisplayScale = 0.0f;
	float ObstacleBoundaryBandCm = 0.0f;
	float BorderFadeRatio = 0.0f;

	float StampDeltaTimeSeconds = 0.0f;
	float StepDeltaTimeSeconds = 0.0f;
	float StampVelocityBlend = 0.0f;
	float VorticityStrength = 0.0f;

	FVector2f AdvectionStepScale = FVector2f::ZeroVector;
	float AdvectVelocityDampingFactor = 1.0f;
	float DensityDissipationFactor = 1.0f;

	float PainterVelocityDecay = 1.0f;
	float TerrainSlopeGravity = 0.0f;
	float FlowNoiseAccelerationCmPerSec2 = 0.0f;
	float AmbientNoiseAmount = 0.0f;

	FIntPoint FlowNoiseLatticeOrigin = FIntPoint::ZeroValue;
	FVector2f FlowNoiseLatticeOffset = FVector2f::ZeroVector;

	FVector2f FlowNoiseLatticeStepPerCell = FVector2f::ZeroVector;
	FVector2f AmbientNoiseLatticeStepPerCell = FVector2f::ZeroVector;

	FIntPoint AmbientNoiseLatticeOrigin = FIntPoint::ZeroValue;
	FVector2f AmbientNoiseLatticeOffset = FVector2f::ZeroVector;

	uint32 NoiseTimeWholePart = 0;
	float NoiseTimeFractionPart = 0.0f;
	float AmbientDensity = 0.0f;
	float AmbientRefillBlend = 0.0f;

	FVector2f BuoyancyVelocityStepCmPerSec = FVector2f::ZeroVector;
	FVector2f PlaneUpDirection = FVector2f::ZeroVector;

	float OutflowBandStart = 0.0f;
	float OutflowBandWidth = 0.0f;
	float OutflowRatePerSec = 0.0f;
	uint32 StampBinSpanCells = 1;

	FIntPoint ShiftCells = FIntPoint::ZeroValue;
	FIntPoint ReadbackTileOrigin = FIntPoint::ZeroValue;

	uint32 StampBinTableOffset = 0;
	uint32 FirstObstacle = 0;
	uint32 ObstacleCount = 0;
	float HeatDissipationFactor = 1.0f;

	FVector2f HeatBuoyancyVelocityStepCmPerSec = FVector2f::ZeroVector;
	float EdgeBounciness = 0.0f;
	float BorderFadeExponent = 1.0f;

	float VortexTangentialStepCmPerSec = 0.0f;
	float VortexRadialDriftCmPerSec = 0.0f;
	float VortexRadiusCm = 0.0f;
	uint32 Padding0 = 0;
};
static_assert(sizeof(FTDFluidGpuActorParameters) == 272, "FTDFluidGpuActorParameters must match the 272 byte shader layout.");
static_assert(offsetof(FTDFluidGpuActorParameters, HeatDissipationFactor) == 236, "FTDFluidGpuActorParameters::HeatDissipationFactor must stay at byte 236.");
static_assert(offsetof(FTDFluidGpuActorParameters, HeatBuoyancyVelocityStepCmPerSec) == 240, "FTDFluidGpuActorParameters::HeatBuoyancyVelocityStepCmPerSec must stay at byte 240.");
static_assert(offsetof(FTDFluidGpuActorParameters, VortexTangentialStepCmPerSec) == 256, "FTDFluidGpuActorParameters::VortexTangentialStepCmPerSec must stay at byte 256.");
static_assert(offsetof(FTDFluidGpuActorParameters, ExtentCm) == 16, "FTDFluidGpuActorParameters::ExtentCm must stay at byte 16.");
static_assert(offsetof(FTDFluidGpuActorParameters, AdvectionStepScale) == 80, "FTDFluidGpuActorParameters::AdvectionStepScale must stay at byte 80.");
static_assert(offsetof(FTDFluidGpuActorParameters, FlowNoiseLatticeOrigin) == 112, "FTDFluidGpuActorParameters::FlowNoiseLatticeOrigin must stay at byte 112.");
static_assert(offsetof(FTDFluidGpuActorParameters, AmbientNoiseLatticeOrigin) == 144, "FTDFluidGpuActorParameters::AmbientNoiseLatticeOrigin must stay at byte 144.");
static_assert(offsetof(FTDFluidGpuActorParameters, NoiseTimeWholePart) == 160, "FTDFluidGpuActorParameters::NoiseTimeWholePart must stay at byte 160.");
static_assert(offsetof(FTDFluidGpuActorParameters, BuoyancyVelocityStepCmPerSec) == 176, "FTDFluidGpuActorParameters::BuoyancyVelocityStepCmPerSec must stay at byte 176.");
static_assert(offsetof(FTDFluidGpuActorParameters, ShiftCells) == 208, "FTDFluidGpuActorParameters::ShiftCells must stay at byte 208.");
static_assert(offsetof(FTDFluidGpuActorParameters, StampBinTableOffset) == 224, "FTDFluidGpuActorParameters::StampBinTableOffset must stay at byte 224.");

struct alignas(16) FTDFluidGpuPackedObstacle
{
	FVector2f CenterFromWindowCenterCm = FVector2f::ZeroVector;
	float RadiusCm = 1.0f;
	uint32 bIsBox = 0;

	FVector2f BoxHalfExtentsCm = FVector2f::ZeroVector;
	float Padding0 = 0.0f;
	float Padding1 = 0.0f;
};
static_assert(sizeof(FTDFluidGpuPackedObstacle) == 32, "FTDFluidGpuPackedObstacle must match the 32 byte shader layout.");
static_assert(offsetof(FTDFluidGpuPackedObstacle, BoxHalfExtentsCm) == 16, "FTDFluidGpuPackedObstacle::BoxHalfExtentsCm must stay at byte 16.");

struct alignas(16) FTDFluidGpuPackedTerrainUpdate
{
	uint32 SliceIndex = 0;
	uint32 CellX = 0;
	uint32 CellY = 0;
	float ElevationCm = 0.0f;
};
static_assert(sizeof(FTDFluidGpuPackedTerrainUpdate) == 16, "FTDFluidGpuPackedTerrainUpdate must match the 16 byte shader layout.");

enum class ETDFluidGpuPressureSolver : uint8
{
	Jacobi,
	Multigrid
};

struct FTDFluidGpuBatchKey
{
	int32 ResolutionTier = 0;
	int32 PressureIterations = 0;
	ETDFluidGpuSimMode SimMode = ETDFluidGpuSimMode::FluidSim;
	ETDFluidGpuPressureSolver PressureSolver = ETDFluidGpuPressureSolver::Jacobi;
	int32 MultigridCycles = 0;
	bool bUsesHeatChannel = false;

	bool operator==(const FTDFluidGpuBatchKey& Other) const
	{
		return ResolutionTier == Other.ResolutionTier
			&& PressureIterations == Other.PressureIterations
			&& SimMode == Other.SimMode
			&& PressureSolver == Other.PressureSolver
			&& MultigridCycles == Other.MultigridCycles
			&& bUsesHeatChannel == Other.bUsesHeatChannel;
	}
};

struct FTDFluidGpuSliceEntry
{
	int32 ActorInputIndex = INDEX_NONE;
	int32 SliceIndex = INDEX_NONE;
	bool bNeedsReset = false;
};

struct FTDFluidGpuBatchFrame
{
	TArray<FTDFluidGpuSliceEntry> Entries;
	TArray<FTDFluidGpuActorParameters> Parameters;
	TArray<uint32> ResetEntryIndices;
	TArray<uint32> ShiftEntryIndices;
	TArray<uint32> ObstacleEntryIndices;
	TArray<FTDFluidGpuPackedObstacle> Obstacles;
	TArray<FTDFluidGpuPackedTerrainUpdate> TerrainUpdates;
};

struct FTDFluidGpuBatchStateTextures
{
	int32 SliceCapacity = 0;
	TRefCountPtr<IPooledRenderTarget> Velocity[2];
	TRefCountPtr<IPooledRenderTarget> Density[2];
	TRefCountPtr<IPooledRenderTarget> SurfaceDisplacement[2];
	TRefCountPtr<IPooledRenderTarget> PressureSolution[2];
	TRefCountPtr<IPooledRenderTarget> ObstacleSdf;
	TRefCountPtr<IPooledRenderTarget> TerrainElevation;
	TRefCountPtr<IPooledRenderTarget> PainterPrimaryDensity;
	TRefCountPtr<IPooledRenderTarget> PainterSelectiveDensity;
	TRefCountPtr<IPooledRenderTarget> Heat[2];
};

struct FTDFluidGpuReadbackAtlasLayout
{
	int32 TileCount = 0;
	int32 TilesPerRow = 0;
	int32 RowCount = 0;

	static FTDFluidGpuReadbackAtlasLayout Make(int32 InTileCount)
	{
		FTDFluidGpuReadbackAtlasLayout Layout;
		Layout.TileCount = FMath::Max(InTileCount, 0);
		if (Layout.TileCount == 0)
		{
			return Layout;
		}

		Layout.TilesPerRow = FMath::Min(Layout.TileCount, TDFluidGpuSolverConstants::ReadbackTilesPerAtlasRow);
		Layout.RowCount = FMath::DivideAndRoundUp(Layout.TileCount, Layout.TilesPerRow);
		return Layout;
	}

	FIntPoint GetAtlasSize() const
	{
		return FIntPoint(TilesPerRow, RowCount) * TDFluidGpuSolverConstants::ReadbackTileResolution;
	}

	FIntPoint GetTileOrigin(int32 TileIndex) const
	{
		if (TilesPerRow <= 0 || TileIndex < 0)
		{
			return FIntPoint::ZeroValue;
		}

		return FIntPoint(TileIndex % TilesPerRow, TileIndex / TilesPerRow) * TDFluidGpuSolverConstants::ReadbackTileResolution;
	}
};

struct FTDFluidGpuPreparedFrame
{
	FTDFluidGpuStampBinSet StampBins;
	FTDFluidGpuReadbackAtlasLayout ReadbackLayout;
};

struct FTDFluidGpuMemoryEstimate
{
	int64 StateBytes = 0;
	int64 TransientBytes = 0;
	int64 OutputBytes = 0;
	int64 ReadbackBytes = 0;

	int64 GetTotalBytes() const
	{
		return StateBytes + TransientBytes + OutputBytes + ReadbackBytes;
	}
};

class TDFLUIDGPU_API FTDFluidGpuBatch
{
public:
	explicit FTDFluidGpuBatch(const FTDFluidGpuBatchKey& InKey);

	static int32 ResolveResolutionTier(int32 RequestedResolution);
	static FTDFluidGpuBatchKey MakeKey(const FTDFluidGpuSimulationSettings& Settings);
	static ETDFluidGpuPressureSolver ResolvePressureSolver(int32 ResolutionTier);
	static int32 ComputeMultigridLevelCount(int32 ResolutionTier);
	static int64 ComputeMultigridTransientBytes(int32 ResolutionTier);
	static int32 RoundUpSliceCapacity(int32 SliceCount);
	static FTDFluidGpuMemoryEstimate EstimateActorMemory(ETDFluidGpuSimMode SimMode, int32 RequestedResolution, bool bUsesHeatChannel = false, bool bUsesTransmittance = false);
	static int32 ComputeTransmittanceResolution(int32 ResolutionTier);
	static void PrepareFrame(const FTDFluidGpuFrameInputs& FrameInputs, TArray<FTDFluidGpuBatch>& InOutBatches, FTDFluidGpuPreparedFrame& OutPreparedFrame);

	const FTDFluidGpuBatchKey& GetKey() const { return Key; }
	const FTDFluidGpuBatchFrame& GetFrame() const { return Frame; }
	int32 FindSlice(uint64 ActorKey) const;
	int32 GetAssignedSliceCount() const { return SliceByActorKey.Num(); }
	int32 GetRequiredSliceCapacity() const;

	FTDFluidGpuBatchStateTextures StateTextures;

private:
	static void ReleaseActorFromBatches(TArray<FTDFluidGpuBatch>& InOutBatches, uint64 ActorKey, const FTDFluidGpuBatchKey* KeptBatchKey);

	int32 AcquireSlice(uint64 ActorKey);
	bool ReleaseSlice(uint64 ActorKey);
	bool ConsumePendingReset(int32 SliceIndex);
	void AddFrameEntry(int32 ActorInputIndex, int32 SliceIndex, bool bShouldForceReset);
	void BuildFrameParameters(const FTDFluidGpuFrameInputs& FrameInputs, FTDFluidGpuPreparedFrame& InOutPreparedFrame);

	FTDFluidGpuBatchKey Key;
	TMap<uint64, int32> SliceByActorKey;
	TArray<bool> SliceOccupancy;
	TArray<bool> SlicePendingReset;
	FTDFluidGpuBatchFrame Frame;
};
