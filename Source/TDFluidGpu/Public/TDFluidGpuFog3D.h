#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "RendererInterface.h"

class FRDGBuilder;
class FRHIGPUBufferReadback;
class FTextureRenderTargetResource;

constexpr int32 TDFluidGpuFog3DLayerCount = 8;
constexpr int32 TDFluidGpuFog3DMaximumSphereCount = 16;
constexpr int32 TDFluidGpuFog3DMaximumObstacleCount = 32;
constexpr int32 TDFluidGpuFog3DMaximumEdgeBandCells = 16;
constexpr int32 TDFluidGpuFog3DDumpFieldCount = 10;
constexpr double TDFluidGpuFog3DWindBlendReferenceStepsPerSecond = 60.0;
constexpr double TDFluidGpuFog3DWindBlendUnitStepTolerance = 1.0e-6;
constexpr uint32 TDFluidGpuFog3DSphereAnchorPresentFlag = 1u;

struct FTDFluidGpuFog3DSphere
{
	FVector4f StartCmRadiusCm = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f EndCmWindStrength = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f VelocityCmPerSecErosionPerSec = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
};

static_assert(sizeof(FTDFluidGpuFog3DSphere) == 48, "FTDFluidGpuFog3DSphere must match the 48 byte shader layout.");

struct FTDFluidGpuFog3DSphereAnchor
{
	int32 CellX = 0;
	int32 CellY = 0;
	float ZCm = 0.0f;
	uint32 Flags = 0u;
};

static_assert(sizeof(FTDFluidGpuFog3DSphereAnchor) == 16, "FTDFluidGpuFog3DSphereAnchor must match the 16 byte shader layout.");

struct FTDFluidGpuFog3DObstacle
{
	FVector4f CenterCmYawCosine = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
	FVector4f HalfExtentCmYawSine = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
};

static_assert(sizeof(FTDFluidGpuFog3DObstacle) == 32, "FTDFluidGpuFog3DObstacle must match the 32 byte shader layout.");

struct FTDFluidGpuFog3DSphereFrame
{
	FVector4f TiltedStartCmReferenceHeightCm = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f TiltedEndCm = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f MidpointCmFrameSlope = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f ReferenceColumnCm = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
};

static_assert(sizeof(FTDFluidGpuFog3DSphereFrame) == 64, "FTDFluidGpuFog3DSphereFrame must match the 64 byte shader layout.");

struct FTDFluidGpuFog3DTuning
{
	float WindRefillPerSec = 0.75f;
	float DisplacementRefillHorizontalPerSec = 0.1f;
	float DisplacementRefillVerticalPerSec = 0.3f;
	float FillRefillPerSec = 0.3f;
	int32 EdgeBandCells = 8;
	float EdgeBandAbsorptionPerSec = 4.0f;
	int32 PressureSweepCount = 8;
	float PressureOverRelaxation = 1.5f;
	float MoveLimitHorizontalCm = 125.0f;
	float MoveLimitVerticalCm = 120.0f;
};

struct FTDFluidGpuFog3DTestKnobs
{
	bool bAppliesPressure = true;
	bool bAppliesRefill = true;
	bool bAppliesEdgeBand = true;
};

struct FTDFluidGpuFog3DStateOverride
{
	TArray<FVector4f> WindFaces;
	TArray<uint8> FillOperations;
};

struct FTDFluidGpuFog3DInflation
{
	float BaseHalfThicknessCm = 7.9f;
	float UpwardExtraCm = 7.2f;
};

struct FTDFluidGpuFog3DGroundUpload
{
	TArray<float> HeightsCm;
	FIntPoint MinCell = FIntPoint::ZeroValue;
	FIntPoint Size = FIntPoint::ZeroValue;
};

struct FTDFluidGpuFog3DGroundCapture
{
	FTextureRenderTargetResource* DepthTarget = nullptr;
	FIntPoint MinCell = FIntPoint::ZeroValue;
	int32 Size = 0;
	float DecodeOffsetCm = 0.0f;
	float MissingHeightCm = 0.0f;
	float MissingBelowFloorCm = 5000.0f;
};

struct FTDFluidGpuFog3DFrameInput
{
	int32 Resolution = 256;
	float CellSizeCm = 15.625f;
	float LayerHeightCm = 30.0f;
	int32 Mode = 2;
	FIntPoint WindowMinCell = FIntPoint::ZeroValue;
	FIntPoint PreviousWindowMinCell = FIntPoint::ZeroValue;
	bool bResetsState = true;
	bool bObstaclesChanged = false;
	float DeltaSeconds = 1.0f / 60.0f;
	FTDFluidGpuFog3DTuning Tuning;
	FTDFluidGpuFog3DTestKnobs TestKnobs;
	TArray<FTDFluidGpuFog3DSphere> Spheres;
	TArray<FTDFluidGpuFog3DObstacle> Obstacles;
	const FTDFluidGpuFog3DStateOverride* StateOverride = nullptr;
	FTextureRenderTargetResource* OutputTarget = nullptr;
	bool bFollowsGround = false;
	bool bUsesClampedTimeWindBlend = false;
	float GroundWallStepCm = 30.0f;
	bool bGroundChanged = false;
	const FTDFluidGpuFog3DGroundUpload* GroundUpload = nullptr;
	FRHIGPUBufferReadback* SphereFrameReadback = nullptr;
	FTDFluidGpuFog3DGroundCapture GroundCapture;
	FTextureRenderTargetResource* GroundMaterialTarget = nullptr;
	bool bOccludesInputBehindObstacles = false;
	TArray<FTDFluidGpuFog3DSphereAnchor> SphereAnchors;
};

class TDFLUIDGPU_API FTDFluidGpuFog3DState
{
public:
	static int64 EstimateStateBytes(int32 Resolution, int32 GroundSize = 0);

	void Release();

	TRefCountPtr<IPooledRenderTarget> WindTargets[2];
	TRefCountPtr<IPooledRenderTarget> DisplacementFillTargets[2];
	TRefCountPtr<IPooledRenderTarget> PressureTarget;
	TRefCountPtr<IPooledRenderTarget> DivergenceTarget;
	TRefCountPtr<IPooledRenderTarget> CellBlockedTarget;
	TRefCountPtr<IPooledRenderTarget> FaceFlagsTarget;
	TRefCountPtr<IPooledRenderTarget> ColumnTerrainTarget;
	TRefCountPtr<IPooledRenderTarget> GroundHeightTarget;
	TRefCountPtr<FRDGPooledBuffer> InputReachBuffer;
	int32 Resolution = 0;
	int32 CurrentWindIndex = 0;
	int32 CurrentDisplacementFillIndex = 0;
	FIntPoint WindowMinCell = FIntPoint::ZeroValue;
	bool bHasStepped = false;
	FIntPoint GroundMinCell = FIntPoint::ZeroValue;
	int32 GroundSize = 0;
	bool bHasGround = false;
};

TDFLUIDGPU_API FTDFluidGpuFog3DObstacle MakeTDFluidGpuFog3DUprightBox(const FVector3d& CenterWindowCm, const FVector3d& HalfExtentCm, double YawDegrees, const FTDFluidGpuFog3DInflation& Inflation);
TDFLUIDGPU_API FTDFluidGpuFog3DSphereAnchor MakeTDFluidGpuFog3DSphereAnchor(const FVector3d& AnchorWindowCm, double CellSizeCm, int32 Resolution);
TDFLUIDGPU_API bool AddTDFluidGpuFog3DPasses(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, const FTDFluidGpuFog3DFrameInput& FrameInput);
TDFLUIDGPU_API int64 GetTDFluidGpuFog3DDumpByteCount(int32 Resolution);
TDFLUIDGPU_API bool AddTDFluidGpuFog3DDumpPass(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, FRHIGPUBufferReadback& Readback);
TDFLUIDGPU_API bool CopyTDFluidGpuFog3DDump(FRHIGPUBufferReadback& Readback, int32 Resolution, TArray<float>& OutValues);
TDFLUIDGPU_API bool AddTDFluidGpuFog3DInputReachDumpPass(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, FRHIGPUBufferReadback& Readback);
TDFLUIDGPU_API bool CopyTDFluidGpuFog3DInputReachDump(FRHIGPUBufferReadback& Readback, int32 Resolution, TArray<uint16>& OutValues);
