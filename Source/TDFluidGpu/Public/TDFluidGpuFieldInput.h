#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumClassFlags.h"
#include "TDFluidGpuInputs.h"

class FRDGBuilder;
class FRDGBufferSRV;
class FRHISamplerState;
class FRHITexture;
class FTDFluidGpuBatch;

enum class ETDFluidGpuFieldFlags : uint32
{
	None = 0,
	SignedVelocity = 1u << 0,
	WritesCollision = 1u << 1,
	FillsInterior = 1u << 2,
	ErasesDensity = 1u << 3,
	Tiled = 1u << 4
};
ENUM_CLASS_FLAGS(ETDFluidGpuFieldFlags);

enum class ETDFluidGpuFieldLayout : uint32
{
	Density = 0,
	DensityVelocity = 1,
	Velocity = 2,
	CollisionMask = 3,
	MeshDistance = 4
};

struct alignas(16) FTDFluidGpuPackedFieldInput
{
	FVector2f CenterFromWindowCenterCm = FVector2f::ZeroVector;
	FVector2f AxisX = FVector2f(1.0f, 0.0f);
	FVector2f InvHalfSizeCm = FVector2f(0.01f, 0.01f);
	FVector2f MotionPlaneCmPerSec = FVector2f::ZeroVector;
	FVector2f UVOffset = FVector2f::ZeroVector;
	float UVTileCount = 1.0f;
	float UVRotationRadians = 0.0f;
	uint32 TextureSlot = 0;
	uint32 Layout = 0;
	uint32 Flags = 0;
	float EraseRatePerSec = 0.0f;
	float DensityScale = 1.0f;
	float VelocityScale = 1.0f;
	float VelocityRangeCmPerSec = 0.0f;
	float CollisionEdgeCm = 1.0f;
	float ContourBandCm = 0.0f;
	float ContourDensityStrength = 0.0f;
	float OutwardVelocityCmPerSec = 0.0f;
	float MotionVelocityStrength = 0.0f;
	float ContourNoiseAmount = 0.0f;
	float InvContourNoiseScaleCm = 0.01f;
	float HalfSizeXCm = 100.0f;
	float HalfSizeYCm = 100.0f;
};
static_assert(sizeof(FTDFluidGpuPackedFieldInput) == 112, "FTDFluidGpuPackedFieldInput must match the HLSL layout");
static_assert(STRUCT_OFFSET(FTDFluidGpuPackedFieldInput, InvHalfSizeCm) == 16, "FTDFluidGpuPackedFieldInput row 1 offset");
static_assert(STRUCT_OFFSET(FTDFluidGpuPackedFieldInput, UVOffset) == 32, "FTDFluidGpuPackedFieldInput row 2 offset");
static_assert(STRUCT_OFFSET(FTDFluidGpuPackedFieldInput, TextureSlot) == 48, "FTDFluidGpuPackedFieldInput row 3 offset");
static_assert(STRUCT_OFFSET(FTDFluidGpuPackedFieldInput, DensityScale) == 64, "FTDFluidGpuPackedFieldInput row 4 offset");
static_assert(STRUCT_OFFSET(FTDFluidGpuPackedFieldInput, ContourBandCm) == 80, "FTDFluidGpuPackedFieldInput row 5 offset");
static_assert(STRUCT_OFFSET(FTDFluidGpuPackedFieldInput, ContourNoiseAmount) == 96, "FTDFluidGpuPackedFieldInput row 6 offset");

struct FTDFluidGpuFieldInputBatchPlan
{
	TArray<FTDFluidGpuPackedFieldInput> Fields;
	TArray<FUintVector2> EntryRanges;
	TArray<FTextureResource*> Textures;
	int32 DroppedCount = 0;
};

struct FTDFluidGpuFieldInputGraph
{
	FRDGBufferSRV* Fields = nullptr;
	FRDGBufferSRV* EntryRanges = nullptr;
	FRHITexture* Textures[TDFluidGpuSolverConstants::MaximumFieldTexturesPerBatch] = {};
	FRHISamplerState* Sampler = nullptr;
};

namespace TDFluidGpuFieldInput
{
	TDFLUIDGPU_API bool IsAllowedByConsole();
	TDFLUIDGPU_API bool HasCollisionField(const FTDFluidGpuActorInput& Actor);
	TDFLUIDGPU_API FTDFluidGpuPackedFieldInput PackField(const FTDFluidGpuFieldInput& Field, const FVector2D& WindowCenterPlaneCm, uint32 TextureSlot);
	TDFLUIDGPU_API FVector2D ResolveFieldUV(const FTDFluidGpuPackedFieldInput& Field, const FVector2D& LocalUnit);
	TDFLUIDGPU_API FTDFluidGpuFieldInputBatchPlan BuildBatchPlan(TConstArrayView<const FTDFluidGpuActorInput*> EntryActors);
	bool PrepareGraph(FRDGBuilder& GraphBuilder, const FTDFluidGpuFrameInputs& FrameInputs, const FTDFluidGpuBatch& Batch, FTDFluidGpuFieldInputGraph& OutGraph);
}

template <typename ParametersType>
void BindTDFluidGpuFieldInputParameters(const FTDFluidGpuFieldInputGraph& Graph, ParametersType& Parameters)
{
	Parameters.FieldInputs = Graph.Fields;
	Parameters.FieldEntryRanges = Graph.EntryRanges;
	Parameters.FieldTexture0 = Graph.Textures[0];
	Parameters.FieldTexture1 = Graph.Textures[1];
	Parameters.FieldTexture2 = Graph.Textures[2];
	Parameters.FieldTexture3 = Graph.Textures[3];
	Parameters.FieldSampler = Graph.Sampler;
}
