#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "Templates/UniquePtr.h"

class FGlobalShaderMap;
class FRHIGPUTextureReadback;
class FTDFluidGpuBatch;
struct FTDFluidGpuFrameInputs;

struct alignas(16) FTDFluidGpuDenseWaterParameters
{
	uint32 Flags = 0;
	int32 DenseResolution = 0;
	int32 DenseDivisor = 1;
	uint32 SubstepCount = 1;

	float SubstepSeconds = 0.0f;
	float GravityCmPerSec2 = 0.0f;
	float GapFillTendency = 0.0f;
	float PeakAvoidance = 0.0f;

	float PeakSoftnessCm = 1.0f;
	float SdfRepelCmPerSec2 = 0.0f;
	float SdfRepelBandCm = 1.0f;
	float LinearDragPerSec = 0.0f;

	float BedFriction = 0.0f;
	float WetThresholdCm = 1.0f;
	float DryDepthCm = 0.05f;
	float FilmDrainCmPerSec = 0.0f;

	float MaxFlowDepthCm = 200.0f;
	float MaxSpeedCmPerSec = 0.0f;
	float MomentumAdvection = 0.0f;
	float ReservoirBlend = 0.0f;

	uint32 SeedTexelOffset = 0;
	int32 SeedResolution = 0;
	uint32 Padding0 = 0;
	uint32 Padding1 = 0;
};
static_assert(sizeof(FTDFluidGpuDenseWaterParameters) == 96, "FTDFluidGpuDenseWaterParameters must match the 96 byte shader layout.");
static_assert(offsetof(FTDFluidGpuDenseWaterParameters, SubstepSeconds) == 16, "FTDFluidGpuDenseWaterParameters::SubstepSeconds must stay at byte 16.");
static_assert(offsetof(FTDFluidGpuDenseWaterParameters, BedFriction) == 48, "FTDFluidGpuDenseWaterParameters::BedFriction must stay at byte 48.");
static_assert(offsetof(FTDFluidGpuDenseWaterParameters, SeedTexelOffset) == 80, "FTDFluidGpuDenseWaterParameters::SeedTexelOffset must stay at byte 80.");

enum class ETDFluidGpuDenseWaterFlags : uint32
{
	None = 0,
	UsesOpenBorders = 1u << 0,
	AppliesStampDelta = 1u << 1,
	HasSeed = 1u << 2
};
ENUM_CLASS_FLAGS(ETDFluidGpuDenseWaterFlags);

struct FTDFluidGpuDenseWaterPassInputs
{
	const FTDFluidGpuBatch* Batch = nullptr;
	const FTDFluidGpuFrameInputs* FrameInputs = nullptr;
	FRDGBufferSRVRef ActorParameters = nullptr;
	FRDGBufferSRVRef WaterParameters = nullptr;
	FRDGTextureRef CompositeSdf = nullptr;
	FRDGTextureRef SurfaceHeightField = nullptr;
	FRDGTextureRef TerrainField = nullptr;
	FRDGTextureRef PublishedVelocity = nullptr;
	FRDGTextureRef StampedVelocity = nullptr;
	FRDGTextureRef DenseDepth[2] = { nullptr, nullptr };
	FRDGTextureRef DenseFaceVelocity[2] = { nullptr, nullptr };
	int32 SliceCapacity = 0;
};

struct FTDFluidGpuDenseWaterPassResult
{
	FRDGTextureRef DenseSurfaceHeight = nullptr;
	int32 FinalStateIndex = 0;
	int32 SubstepCount = 0;
	int32 SubstepCapHitCount = 0;
};

TArray<FTDFluidGpuDenseWaterParameters> MakeTDFluidGpuDenseWaterParameters(const FTDFluidGpuBatch& Batch, const FTDFluidGpuFrameInputs& FrameInputs, TArray<FVector4f>& OutSeedTexels, TArray<uint32>& OutSeedEntryIndices, int32& OutSubstepCapHitCount);
FTDFluidGpuDenseWaterPassResult AddTDFluidGpuDenseWaterSolvePasses(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const FTDFluidGpuDenseWaterPassInputs& Inputs);
void AddTDFluidGpuDenseWaterDepthProbePasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuBatch& Batch, int32 EntryIndex, TUniquePtr<FRHIGPUTextureReadback>& OutDepthReadback);
