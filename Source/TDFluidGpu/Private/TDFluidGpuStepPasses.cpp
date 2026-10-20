#include "TDFluidGpuStepPasses.h"

#include "TDFluidGpuMultigridProbeReadbacks.h"
#include "TDFluidGpuPassTimestamps.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "PooledRenderTarget.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"
#include "TextureResource.h"

DECLARE_GPU_STAT(TDFluidGpuSolver);

namespace
{
	constexpr int32 CellGroupSize = 8;
	constexpr int32 TileGroupSize = 16;
	constexpr int32 JacobiIterationsPerDispatch = 4;
	constexpr int32 TerrainUpdateGroupSize = 64;
	constexpr int32 ShiftPhaseGather = 0;
	constexpr int32 ShiftPhaseScatter = 1;
	constexpr int32 ShiftPhaseResetOnly = 2;
	constexpr EPixelFormat VelocityFormat = PF_G16R16F;
	constexpr EPixelFormat DensityFormat = PF_G32R32F;
	constexpr EPixelFormat SurfaceDisplacementFormat = PF_R16F;
	constexpr EPixelFormat PressureSolutionFormat = PF_R32_FLOAT;
	constexpr EPixelFormat ObstacleSdfFormat = PF_R16F;
	constexpr EPixelFormat TerrainElevationFormat = PF_R32_FLOAT;
	constexpr EPixelFormat DivergenceFormat = PF_R16F;
	constexpr EPixelFormat PainterDensityFormat = PF_R32_FLOAT;
	constexpr EPixelFormat PainterShiftStageFormat = PF_G32R32F;
	constexpr EPixelFormat TerrainShiftStageFormat = PF_R32_FLOAT;
	constexpr EPixelFormat OutputFormat = PF_FloatRGBA;
	constexpr EPixelFormat ReadbackAtlasFormat = PF_FloatRGBA;
	constexpr EPixelFormat MultigridValueFormat = PF_R32_FLOAT;
	constexpr EPixelFormat MultigridApertureFormat = PF_G16R16F;
	constexpr EPixelFormat MultigridComponentLabelFormat = PF_R32_UINT;
	constexpr EPixelFormat HeatFormat = PF_R32_FLOAT;
	constexpr int32 TransmittanceGroupSize = 8;
}

class FTDFluidGpuComputeShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FTDFluidGpuComputeShader() {}
	FTDFluidGpuComputeShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};

class FTDFluidGpuShiftCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuShiftCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuShiftCS, FTDFluidGpuComputeShader);

	class FPhaseDimension : SHADER_PERMUTATION_INT("TDFLUIDGPU_SHIFT_PHASE", 3);
	class FPainterDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_PAINTER");
	using FPermutationDomain = TShaderPermutationDomain<FPhaseDimension, FPainterDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, EntryList)
		SHADER_PARAMETER(uint32, ShiftEntryCount)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PainterPrimaryDensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PainterSelectiveDensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, PainterStageOut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, PainterStageIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PainterPrimaryDensityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PainterSelectiveDensityOut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, SurfaceDisplacementIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureSolutionIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, TerrainElevationIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityStageOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, DensityStageOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, SurfaceDisplacementStageOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PressureSolutionStageOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, TerrainStageOut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityStageIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityStageIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, SurfaceDisplacementStageIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureSolutionStageIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, TerrainStageIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, DensityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, SurfaceDisplacementOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PressureSolutionOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, TerrainElevationOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, ObstacleSdfOut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, HeatIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, HeatStageOut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, HeatStageIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, HeatOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuShiftCS, "/TDFluidGpu/Private/TDFluidGpuShift.usf", "MainCS", SF_Compute);

class FTDFluidGpuTerrainUpdateCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuTerrainUpdateCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuTerrainUpdateCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedTerrainUpdate>, TerrainUpdates)
		SHADER_PARAMETER(uint32, TerrainUpdateCount)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, TerrainElevationUpdateOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuTerrainUpdateCS, "/TDFluidGpu/Private/TDFluidGpuShift.usf", "TerrainUpdateCS", SF_Compute);

class FTDFluidGpuRasterizeObstaclesCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuRasterizeObstaclesCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuRasterizeObstaclesCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, EntryList)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedObstacle>, Obstacles)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, ObstacleSdfOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuRasterizeObstaclesCS, "/TDFluidGpu/Private/TDFluidGpuRasterizeObstacles.usf", "MainCS", SF_Compute);

class FTDFluidGpuApplyStampsCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuApplyStampsCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuApplyStampsCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedStamp>, Stamps)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuStampBinRange>, StampBinRanges)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, StampBinIndices)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, SurfaceDisplacementIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, DensityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, SurfaceDisplacementOut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, HeatIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, HeatOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuApplyStampsCS, "/TDFluidGpu/Private/TDFluidGpuApplyStamps.usf", "MainCS", SF_Compute);

class FTDFluidGpuForcesCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuForcesCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuForcesCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, TerrainElevation)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, HeatIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuForcesCS, "/TDFluidGpu/Private/TDFluidGpuForces.usf", "ForcesCS", SF_Compute);

class FTDFluidGpuVorticityCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuVorticityCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuVorticityCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuVorticityCS, "/TDFluidGpu/Private/TDFluidGpuForces.usf", "VorticityCS", SF_Compute);

class FTDFluidGpuJacobiTileCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuJacobiTileCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuJacobiTileCS, FTDFluidGpuComputeShader);

	class FComputesDivergenceDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_COMPUTES_DIVERGENCE");
	using FPermutationDomain = TShaderPermutationDomain<FComputesDivergenceDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, DivergenceIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, DivergenceOut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PressureOut)
		SHADER_PARAMETER(uint32, IterationCount)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuJacobiTileCS, "/TDFluidGpu/Private/TDFluidGpuPressure.usf", "JacobiTileCS", SF_Compute);

class FTDFluidGpuSubtractGradientCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuSubtractGradientCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuSubtractGradientCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
		SHADER_PARAMETER(uint32, EnforcesObstacleBoundaries)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuSubtractGradientCS, "/TDFluidGpu/Private/TDFluidGpuPressure.usf", "SubtractGradientCS", SF_Compute);

class FTDFluidGpuPressureMeanCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuPressureMeanCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuPressureMeanCS, FTDFluidGpuComputeShader);

	class FMeanPhaseDimension : SHADER_PERMUTATION_INT("TDFLUIDGPU_MEAN_PHASE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FMeanPhaseDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, PressurePartialSumsIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, PressurePartialSumsOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, MeanPressureOut)
		SHADER_PARAMETER(uint32, TilesPerSlice)
		SHADER_PARAMETER(uint32, TilesPerRow)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuPressureMeanCS, "/TDFluidGpu/Private/TDFluidGpuPressure.usf", "PressureMeanCS", SF_Compute);

class FTDFluidGpuMultigridDownCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuMultigridDownCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuMultigridDownCS, FTDFluidGpuComputeShader);

	class FFinestLevelDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_MG_IS_FINEST_LEVEL");
	class FWritesCoarseAperturesDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_MG_WRITES_COARSE_APERTURES");
	using FPermutationDomain = TShaderPermutationDomain<FFinestLevelDimension, FWritesCoarseAperturesDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ValueIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, RhsIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, AperturesIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, ValueOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, CoarseRhsOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, CoarseAperturesOut)
		SHADER_PARAMETER(float, LevelWeightScale)
		SHADER_PARAMETER(float, WarmStartScale)
		SHADER_PARAMETER(int32, LevelResolution)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuMultigridDownCS, "/TDFluidGpu/Private/TDFluidGpuMultigrid.usf", "MultigridDownCS", SF_Compute);

class FTDFluidGpuMultigridUpCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuMultigridUpCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuMultigridUpCS, FTDFluidGpuComputeShader);

	class FFinestLevelDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_MG_IS_FINEST_LEVEL");
	class FWritesPressurePartialSumsDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_MG_WRITES_PRESSURE_PARTIAL_SUMS");
	using FPermutationDomain = TShaderPermutationDomain<FFinestLevelDimension, FWritesPressurePartialSumsDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ValueIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, RhsIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, AperturesIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CoarseCorrectionIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, CoarseAperturesIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, ValueOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVector2f>, PressurePartialSumsOut)
		SHADER_PARAMETER(float, LevelWeightScale)
		SHADER_PARAMETER(int32, LevelResolution)
		SHADER_PARAMETER(uint32, TilesPerSlice)
		SHADER_PARAMETER(uint32, TilesPerRow)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuMultigridUpCS, "/TDFluidGpu/Private/TDFluidGpuMultigrid.usf", "MultigridUpCS", SF_Compute);

class FTDFluidGpuMultigridCoarseSolveCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuMultigridCoarseSolveCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuMultigridCoarseSolveCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, RhsIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, AperturesIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, CorrectionOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<uint>, ComponentLabels)
		SHADER_PARAMETER(float, LevelWeightScale)
		SHADER_PARAMETER(uint32, ComputesComponentLabels)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuMultigridCoarseSolveCS, "/TDFluidGpu/Private/TDFluidGpuMultigridCoarse.usf", "MultigridCoarseSolveCS", SF_Compute);

class FTDFluidGpuAdvectVelocityCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuAdvectVelocityCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuAdvectVelocityCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuAdvectVelocityCS, "/TDFluidGpu/Private/TDFluidGpuAdvectVelocity.usf", "MainCS", SF_Compute);

class FTDFluidGpuAdvectDensityPackCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuAdvectDensityPackCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuAdvectDensityPackCS, FTDFluidGpuComputeShader);

	class FWritesOutputDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_WRITES_OUTPUT");
	using FPermutationDomain = TShaderPermutationDomain<FWritesOutputDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedStamp>, Stamps)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuStampBinRange>, StampBinRanges)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, StampBinIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, MeanPressure)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, TerrainElevation)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, SurfaceDisplacementIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, DensityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, SurfaceDisplacementOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PressureSolution)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DensityPressureOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, VelocityOutput)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, HeatIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, HeatOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, AuxOutput)
		SHADER_PARAMETER(uint32, EntryIndex)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuAdvectDensityPackCS, "/TDFluidGpu/Private/TDFluidGpuAdvectDensityPack.usf", "MainCS", SF_Compute);

class FTDFluidGpuTransmittanceCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuTransmittanceCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuTransmittanceCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, DensityPressureTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, TransmittanceOutput)
		SHADER_PARAMETER(FVector4f, VolumeBoxSizeCm)
		SHADER_PARAMETER(FVector4f, LightPositionLocalCm)
		SHADER_PARAMETER(FVector4f, LightDirectionLocal)
		SHADER_PARAMETER(int32, SimResolution)
		SHADER_PARAMETER(int32, OutputResolution)
		SHADER_PARAMETER(int32, MarchStepCount)
		SHADER_PARAMETER(int32, LightMode)
		SHADER_PARAMETER(uint32, IsSymmetric)
		SHADER_PARAMETER(float, DensityScale)
		SHADER_PARAMETER(float, SoftFalloffCm)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuTransmittanceCS, "/TDFluidGpu/Private/TDFluidGpuTransmittance.usf", "MainCS", SF_Compute);

class FTDFluidGpuReadbackDownsampleCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuReadbackDownsampleCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuReadbackDownsampleCS, FTDFluidGpuComputeShader);

	class FPainterDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_PAINTER");
	using FPermutationDomain = TShaderPermutationDomain<FPainterDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PainterPrimaryDensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PainterSelectiveDensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ReadbackAtlasOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuReadbackDownsampleCS, "/TDFluidGpu/Private/TDFluidGpuReadback.usf", "MainCS", SF_Compute);

class FTDFluidGpuPainterStepCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuPainterStepCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuPainterStepCS, FTDFluidGpuComputeShader);

	class FWritesOutputDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_WRITES_OUTPUT");
	using FPermutationDomain = TShaderPermutationDomain<FWritesOutputDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedStamp>, Stamps)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuStampBinRange>, StampBinRanges)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, StampBinIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedObstacle>, Obstacles)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PainterPrimaryDensity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PainterSelectiveDensity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DensityPressureOutput)
		SHADER_PARAMETER(uint32, EntryIndex)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuPainterStepCS, "/TDFluidGpu/Private/TDFluidGpuPainter.usf", "MainCS", SF_Compute);

namespace
{
	struct FTDFluidGpuFrameResources
	{
		FRDGBufferSRVRef Stamps = nullptr;
		FRDGBufferSRVRef StampBinRanges = nullptr;
		FRDGBufferSRVRef StampBinIndices = nullptr;
		FRDGTextureRef ReadbackAtlas = nullptr;
		FRDGTextureUAVRef ReadbackAtlasUAV = nullptr;
	};

	struct FTDFluidGpuBatchGraphTextures
	{
		FRDGTextureRef Velocity[2] = { nullptr, nullptr };
		FRDGTextureRef Density[2] = { nullptr, nullptr };
		FRDGTextureRef SurfaceDisplacement[2] = { nullptr, nullptr };
		FRDGTextureRef PressureSolution[2] = { nullptr, nullptr };
		FRDGTextureRef ObstacleSdf = nullptr;
		FRDGTextureRef TerrainElevation = nullptr;
		FRDGTextureRef PainterPrimaryDensity = nullptr;
		FRDGTextureRef PainterSelectiveDensity = nullptr;
		FRDGTextureRef Heat[2] = { nullptr, nullptr };
	};

	struct FTDFluidGpuBatchPassContext
	{
		FTDFluidGpuBatchPassContext(FRDGBuilder& InGraphBuilder, FGlobalShaderMap* InShaderMap, const FTDFluidGpuFrameInputs& InFrameInputs, const FTDFluidGpuFrameResources& InFrameResources, const FTDFluidGpuBatch& InBatch, const FTDFluidGpuBatchGraphTextures& InTextures, FRDGBufferSRVRef InActorParameters, FRDGBufferSRVRef InObstacles, FTDFluidGpuPassTimestamps* InPassTimestamps)
			: GraphBuilder(InGraphBuilder)
			, ShaderMap(InShaderMap)
			, FrameInputs(InFrameInputs)
			, FrameResources(InFrameResources)
			, Batch(InBatch)
			, Textures(InTextures)
			, ActorParameters(InActorParameters)
			, Obstacles(InObstacles)
			, Resolution(InBatch.GetKey().ResolutionTier)
			, EntryCount(InBatch.GetFrame().Entries.Num())
			, PassTimestamps(InPassTimestamps)
		{
		}

		FRDGBuilder& GraphBuilder;
		FGlobalShaderMap* ShaderMap;
		const FTDFluidGpuFrameInputs& FrameInputs;
		const FTDFluidGpuFrameResources& FrameResources;
		const FTDFluidGpuBatch& Batch;
		const FTDFluidGpuBatchGraphTextures& Textures;
		FRDGBufferSRVRef ActorParameters;
		FRDGBufferSRVRef Obstacles;
		int32 Resolution;
		int32 EntryCount;
		FTDFluidGpuPassTimestamps* PassTimestamps;
	};

	struct FTDFluidGpuOutputTargets
	{
		FRDGTextureRef DensityPressure = nullptr;
		FRDGTextureRef Velocity = nullptr;
	};

	struct FTDFluidGpuEntryDensityOutput
	{
		int32 EntryIndex = INDEX_NONE;
		FRDGTextureRef DensityPressure = nullptr;
	};

	void AddPassTimestamp(FRDGBuilder& GraphBuilder, FTDFluidGpuPassTimestamps* PassTimestamps, const TCHAR* Label, int32 LevelSuffix = INDEX_NONE)
	{
		if (!PassTimestamps)
		{
			return;
		}

		FRenderQueryRHIRef Query = PassTimestamps->Queries.Add_GetRef(RHICreateRenderQuery(RQT_AbsoluteTime));
		PassTimestamps->Labels.Add(LevelSuffix == INDEX_NONE ? FString(Label) : FString::Printf(TEXT("%s%d"), Label, LevelSuffix));
		GraphBuilder.AddPass(RDG_EVENT_NAME("TDFluidGpu.Timestamp %s", Label), ERDGPassFlags::None, [Query](FRHICommandList& RHICmdList)
		{
			RHICmdList.EndRenderQuery(Query);
		});
	}

	FRDGTextureSRVRef MakeSRV(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
	{
		return GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Texture));
	}

	FRDGTextureUAVRef MakeUAV(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
	{
		return GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Texture));
	}

	FRDGTextureUAVRef MakeUnorderedUAV(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
	{
		return GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Texture), ERDGUnorderedAccessViewFlags::SkipBarrier);
	}

	FRDGTextureDesc MakeStateArrayDesc(int32 Resolution, int32 SliceCount, EPixelFormat Format)
	{
		return FRDGTextureDesc::Create2DArray(FIntPoint(Resolution, Resolution), Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, static_cast<uint16>(SliceCount));
	}

	FIntVector MakeCellGroupCount(int32 Resolution, int32 SliceCount)
	{
		return FIntVector(FMath::DivideAndRoundUp(Resolution, CellGroupSize), FMath::DivideAndRoundUp(Resolution, CellGroupSize), SliceCount);
	}

	FIntVector MakeTileGroupCount(int32 Resolution, int32 SliceCount)
	{
		return FIntVector(FMath::DivideAndRoundUp(Resolution, TileGroupSize), FMath::DivideAndRoundUp(Resolution, TileGroupSize), SliceCount);
	}

	bool IsPainterBatch(const FTDFluidGpuBatch& Batch)
	{
		return Batch.GetKey().SimMode == ETDFluidGpuSimMode::SimplePainter;
	}

	bool IsStateTextureReusable(const TRefCountPtr<IPooledRenderTarget>& PooledTexture, int32 Resolution, int32 SliceCapacity)
	{
		if (!PooledTexture.IsValid())
		{
			return false;
		}

		const FPooledRenderTargetDesc& Desc = PooledTexture->GetDesc();
		return Desc.Extent == FIntPoint(Resolution, Resolution) && Desc.ArraySize >= SliceCapacity;
	}

	FRDGTextureRef RegisterOrGrowStateTexture(FRDGBuilder& GraphBuilder, TRefCountPtr<IPooledRenderTarget>& InOutPooledTexture, int32 Resolution, int32 SliceCapacity, EPixelFormat Format, bool bPreservesSlices, const TCHAR* Name)
	{
		if (IsStateTextureReusable(InOutPooledTexture, Resolution, SliceCapacity))
		{
			return GraphBuilder.RegisterExternalTexture(InOutPooledTexture, Name);
		}

		FRDGTextureRef NewTexture = GraphBuilder.CreateTexture(MakeStateArrayDesc(Resolution, SliceCapacity, Format), Name);
		const bool bCopiesOldSlices = bPreservesSlices && InOutPooledTexture.IsValid() && InOutPooledTexture->GetDesc().Extent == FIntPoint(Resolution, Resolution);
		if (bCopiesOldSlices)
		{
			FRDGTextureRef OldTexture = GraphBuilder.RegisterExternalTexture(InOutPooledTexture, Name);
			FRHICopyTextureInfo CopyInfo;
			CopyInfo.Size = FIntVector(Resolution, Resolution, 1);
			CopyInfo.NumSlices = static_cast<uint32>(FMath::Min(static_cast<int32>(InOutPooledTexture->GetDesc().ArraySize), SliceCapacity));
			AddCopyTexturePass(GraphBuilder, OldTexture, NewTexture, CopyInfo);
		}

		InOutPooledTexture = GraphBuilder.ConvertToExternalTexture(NewTexture);
		return NewTexture;
	}

	FRDGTextureRef CreateClearedPlaceholderHeatTexture(FRDGBuilder& GraphBuilder, const TCHAR* Name)
	{
		FRDGTextureRef Placeholder = GraphBuilder.CreateTexture(MakeStateArrayDesc(1, 1, HeatFormat), Name);
		AddClearUAVPass(GraphBuilder, MakeUAV(GraphBuilder, Placeholder), FLinearColor::Transparent);
		return Placeholder;
	}

	void RegisterHeatTextures(FRDGBuilder& GraphBuilder, FTDFluidGpuBatch& Batch, int32 SliceCapacity, FTDFluidGpuBatchGraphTextures& InOutTextures)
	{
		FTDFluidGpuBatchStateTextures& State = Batch.StateTextures;
		if (!Batch.GetKey().bUsesHeatChannel)
		{
			State.Heat[0].SafeRelease();
			State.Heat[1].SafeRelease();
			InOutTextures.Heat[0] = CreateClearedPlaceholderHeatTexture(GraphBuilder, TEXT("TDFluidGpu.HeatPlaceholder"));
			InOutTextures.Heat[1] = CreateClearedPlaceholderHeatTexture(GraphBuilder, TEXT("TDFluidGpu.HeatScratchPlaceholder"));
			return;
		}

		const int32 Resolution = Batch.GetKey().ResolutionTier;
		InOutTextures.Heat[0] = RegisterOrGrowStateTexture(GraphBuilder, State.Heat[0], Resolution, SliceCapacity, HeatFormat, true, TEXT("TDFluidGpu.Heat"));
		InOutTextures.Heat[1] = RegisterOrGrowStateTexture(GraphBuilder, State.Heat[1], Resolution, SliceCapacity, HeatFormat, false, TEXT("TDFluidGpu.HeatScratch"));
	}

	FTDFluidGpuBatchGraphTextures RegisterBatchStateTextures(FRDGBuilder& GraphBuilder, FTDFluidGpuBatch& Batch)
	{
		const int32 Resolution = Batch.GetKey().ResolutionTier;
		const int32 SliceCapacity = FMath::Max(Batch.StateTextures.SliceCapacity, Batch.GetRequiredSliceCapacity());
		FTDFluidGpuBatchStateTextures& State = Batch.StateTextures;
		FTDFluidGpuBatchGraphTextures Textures;
		if (IsPainterBatch(Batch))
		{
			Textures.PainterPrimaryDensity = RegisterOrGrowStateTexture(GraphBuilder, State.PainterPrimaryDensity, Resolution, SliceCapacity, PainterDensityFormat, true, TEXT("TDFluidGpu.PainterPrimaryDensity"));
			Textures.PainterSelectiveDensity = RegisterOrGrowStateTexture(GraphBuilder, State.PainterSelectiveDensity, Resolution, SliceCapacity, PainterDensityFormat, true, TEXT("TDFluidGpu.PainterSelectiveDensity"));
			State.SliceCapacity = SliceCapacity;
			return Textures;
		}

		Textures.Velocity[0] = RegisterOrGrowStateTexture(GraphBuilder, State.Velocity[0], Resolution, SliceCapacity, VelocityFormat, true, TEXT("TDFluidGpu.Velocity"));
		Textures.Velocity[1] = RegisterOrGrowStateTexture(GraphBuilder, State.Velocity[1], Resolution, SliceCapacity, VelocityFormat, false, TEXT("TDFluidGpu.VelocityScratch"));
		Textures.Density[0] = RegisterOrGrowStateTexture(GraphBuilder, State.Density[0], Resolution, SliceCapacity, DensityFormat, true, TEXT("TDFluidGpu.Density"));
		Textures.Density[1] = RegisterOrGrowStateTexture(GraphBuilder, State.Density[1], Resolution, SliceCapacity, DensityFormat, false, TEXT("TDFluidGpu.DensityScratch"));
		Textures.SurfaceDisplacement[0] = RegisterOrGrowStateTexture(GraphBuilder, State.SurfaceDisplacement[0], Resolution, SliceCapacity, SurfaceDisplacementFormat, true, TEXT("TDFluidGpu.SurfaceDisplacement"));
		Textures.SurfaceDisplacement[1] = RegisterOrGrowStateTexture(GraphBuilder, State.SurfaceDisplacement[1], Resolution, SliceCapacity, SurfaceDisplacementFormat, false, TEXT("TDFluidGpu.SurfaceDisplacementScratch"));
		Textures.PressureSolution[0] = RegisterOrGrowStateTexture(GraphBuilder, State.PressureSolution[0], Resolution, SliceCapacity, PressureSolutionFormat, true, TEXT("TDFluidGpu.PressureSolution"));
		Textures.PressureSolution[1] = RegisterOrGrowStateTexture(GraphBuilder, State.PressureSolution[1], Resolution, SliceCapacity, PressureSolutionFormat, false, TEXT("TDFluidGpu.PressureSolutionScratch"));
		Textures.ObstacleSdf = RegisterOrGrowStateTexture(GraphBuilder, State.ObstacleSdf, Resolution, SliceCapacity, ObstacleSdfFormat, true, TEXT("TDFluidGpu.ObstacleSdf"));
		Textures.TerrainElevation = RegisterOrGrowStateTexture(GraphBuilder, State.TerrainElevation, Resolution, SliceCapacity, TerrainElevationFormat, true, TEXT("TDFluidGpu.TerrainElevation"));
		RegisterHeatTextures(GraphBuilder, Batch, SliceCapacity, Textures);
		State.SliceCapacity = SliceCapacity;
		return Textures;
	}

	TArray<FTDFluidGpuPackedTerrainUpdate> KeepLastTerrainUpdatePerCell(const TArray<FTDFluidGpuPackedTerrainUpdate>& Updates)
	{
		TArray<FTDFluidGpuPackedTerrainUpdate> UniqueUpdates;
		UniqueUpdates.Reserve(Updates.Num());
		TMap<FIntVector, int32> UpdateIndexByCell;
		UpdateIndexByCell.Reserve(Updates.Num());
		for (const FTDFluidGpuPackedTerrainUpdate& Update : Updates)
		{
			const FIntVector CellKey(static_cast<int32>(Update.CellX), static_cast<int32>(Update.CellY), static_cast<int32>(Update.SliceIndex));
			if (const int32* ExistingIndex = UpdateIndexByCell.Find(CellKey))
			{
				UniqueUpdates[*ExistingIndex] = Update;
				continue;
			}

			UpdateIndexByCell.Add(CellKey, UniqueUpdates.Add(Update));
		}
		return UniqueUpdates;
	}

	FRDGBufferSRVRef UploadEntryList(FRDGBuilder& GraphBuilder, const TCHAR* Name, TArray<uint32>&& EntryIndices)
	{
		return GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, Name, MoveTemp(EntryIndices)));
	}

	bool IsUsableOutputTexture(const FRHITexture* Texture, int32 Resolution)
	{
		if (!Texture)
		{
			return false;
		}

		const FRHITextureDesc& Desc = Texture->GetDesc();
		return Desc.Dimension == ETextureDimension::Texture2D
			&& Desc.Extent == FIntPoint(Resolution, Resolution)
			&& Desc.Format == OutputFormat
			&& EnumHasAnyFlags(Desc.Flags, TexCreate_UAV);
	}

	FRDGTextureRef RegisterOutputTexture(FRDGBuilder& GraphBuilder, FTextureRenderTargetResource* Resource, int32 Resolution, const TCHAR* Name)
	{
		if (!Resource)
		{
			return nullptr;
		}

		FRHITexture* RHITexture = Resource->GetRenderTargetTexture().GetReference();
		if (!IsUsableOutputTexture(RHITexture, Resolution))
		{
			return nullptr;
		}

		FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(RHITexture, Name));
		GraphBuilder.SetTextureAccessFinal(OutputTexture, ERHIAccess::SRVMask);
		return OutputTexture;
	}

	FRDGTextureRef RegisterSizedOutputTexture(FRDGBuilder& GraphBuilder, FTextureRenderTargetResource* Resource, int32 Size, const TCHAR* Name)
	{
		return RegisterOutputTexture(GraphBuilder, Resource, Size, Name);
	}

	FTDFluidGpuOutputTargets RegisterOutputTargets(FRDGBuilder& GraphBuilder, const FTDFluidGpuActorInput& Actor, int32 Resolution, bool bNeedsVelocityOutput)
	{
		FTDFluidGpuOutputTargets Targets;
		Targets.DensityPressure = RegisterOutputTexture(GraphBuilder, Actor.DensityPressureOutput, Resolution, TEXT("TDFluidGpu.DensityPressureOutput"));
		if (!bNeedsVelocityOutput)
		{
			return Targets;
		}

		Targets.Velocity = RegisterOutputTexture(GraphBuilder, Actor.VelocityOutput, Resolution, TEXT("TDFluidGpu.VelocityOutput"));
		if (!Targets.Velocity)
		{
			Targets.DensityPressure = nullptr;
		}
		return Targets;
	}

	FTDFluidGpuFrameResources CreateFrameResources(FRDGBuilder& GraphBuilder, const FTDFluidGpuPreparedFrame& PreparedFrame)
	{
		FTDFluidGpuFrameResources Resources;
		Resources.Stamps = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Stamps"), PreparedFrame.StampBins.Stamps));
		Resources.StampBinRanges = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.StampBinRanges"), PreparedFrame.StampBins.BinRanges));
		Resources.StampBinIndices = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.StampBinIndices"), PreparedFrame.StampBins.BinStampIndices));

		const FIntPoint AtlasSize = PreparedFrame.ReadbackLayout.GetAtlasSize();
		if (AtlasSize.X <= 0 || AtlasSize.Y <= 0)
		{
			return Resources;
		}

		Resources.ReadbackAtlas = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(AtlasSize, ReadbackAtlasFormat, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV), TEXT("TDFluidGpu.ReadbackAtlas"));
		AddClearUAVPass(GraphBuilder, MakeUAV(GraphBuilder, Resources.ReadbackAtlas), FLinearColor::Transparent);
		Resources.ReadbackAtlasUAV = MakeUnorderedUAV(GraphBuilder, Resources.ReadbackAtlas);
		return Resources;
	}

	void AddShiftGatherPass(const FTDFluidGpuBatchPassContext& Context, FRDGTextureRef CompactStage)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchFrame& Frame = Context.Batch.GetFrame();
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		const bool bIsPainter = IsPainterBatch(Context.Batch);

		FTDFluidGpuShiftCS::FPermutationDomain Permutation;
		Permutation.Set<FTDFluidGpuShiftCS::FPhaseDimension>(ShiftPhaseGather);
		Permutation.Set<FTDFluidGpuShiftCS::FPainterDimension>(bIsPainter);

		FTDFluidGpuShiftCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuShiftCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->EntryList = UploadEntryList(GraphBuilder, TEXT("TDFluidGpu.ShiftEntries"), TArray<uint32>(Frame.ShiftEntryIndices));
		Parameters->ShiftEntryCount = static_cast<uint32>(Frame.ShiftEntryIndices.Num());
		if (bIsPainter)
		{
			Parameters->PainterPrimaryDensityIn = MakeSRV(GraphBuilder, Textures.PainterPrimaryDensity);
			Parameters->PainterSelectiveDensityIn = MakeSRV(GraphBuilder, Textures.PainterSelectiveDensity);
			Parameters->PainterStageOut = MakeUAV(GraphBuilder, CompactStage);
		}
		else
		{
			Parameters->VelocityIn = MakeSRV(GraphBuilder, Textures.Velocity[0]);
			Parameters->DensityIn = MakeSRV(GraphBuilder, Textures.Density[0]);
			Parameters->SurfaceDisplacementIn = MakeSRV(GraphBuilder, Textures.SurfaceDisplacement[0]);
			Parameters->PressureSolutionIn = MakeSRV(GraphBuilder, Textures.PressureSolution[0]);
			Parameters->TerrainElevationIn = MakeSRV(GraphBuilder, Textures.TerrainElevation);
			Parameters->VelocityStageOut = MakeUAV(GraphBuilder, Textures.Velocity[1]);
			Parameters->DensityStageOut = MakeUAV(GraphBuilder, Textures.Density[1]);
			Parameters->SurfaceDisplacementStageOut = MakeUAV(GraphBuilder, Textures.SurfaceDisplacement[1]);
			Parameters->PressureSolutionStageOut = MakeUAV(GraphBuilder, Textures.PressureSolution[1]);
			Parameters->TerrainStageOut = MakeUAV(GraphBuilder, CompactStage);
			Parameters->HeatIn = MakeSRV(GraphBuilder, Textures.Heat[0]);
			Parameters->HeatStageOut = MakeUAV(GraphBuilder, Textures.Heat[1]);
		}

		TShaderMapRef<FTDFluidGpuShiftCS> Shader(Context.ShaderMap, Permutation);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.ShiftGather"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, Frame.ShiftEntryIndices.Num()));
	}

	void AddShiftAndResetPasses(const FTDFluidGpuBatchPassContext& Context)
	{
		const FTDFluidGpuBatchFrame& Frame = Context.Batch.GetFrame();
		const int32 ShiftCount = Frame.ShiftEntryIndices.Num();
		const int32 ScatterCount = ShiftCount + Frame.ResetEntryIndices.Num();
		if (ScatterCount == 0)
		{
			return;
		}

		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const bool bIsPainter = IsPainterBatch(Context.Batch);
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		FRDGTextureRef CompactStage = nullptr;
		if (ShiftCount > 0)
		{
			CompactStage = GraphBuilder.CreateTexture(MakeStateArrayDesc(Context.Resolution, ShiftCount, bIsPainter ? PainterShiftStageFormat : TerrainShiftStageFormat), TEXT("TDFluidGpu.ShiftStage"));
			AddShiftGatherPass(Context, CompactStage);
		}

		TArray<uint32> ScatterEntries(Frame.ShiftEntryIndices);
		ScatterEntries.Append(Frame.ResetEntryIndices);

		FTDFluidGpuShiftCS::FPermutationDomain Permutation;
		Permutation.Set<FTDFluidGpuShiftCS::FPhaseDimension>(CompactStage ? ShiftPhaseScatter : ShiftPhaseResetOnly);
		Permutation.Set<FTDFluidGpuShiftCS::FPainterDimension>(bIsPainter);

		FTDFluidGpuShiftCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuShiftCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->EntryList = UploadEntryList(GraphBuilder, TEXT("TDFluidGpu.ShiftScatterEntries"), MoveTemp(ScatterEntries));
		Parameters->ShiftEntryCount = static_cast<uint32>(ShiftCount);
		if (bIsPainter)
		{
			Parameters->PainterPrimaryDensityOut = MakeUAV(GraphBuilder, Textures.PainterPrimaryDensity);
			Parameters->PainterSelectiveDensityOut = MakeUAV(GraphBuilder, Textures.PainterSelectiveDensity);
			if (CompactStage)
			{
				Parameters->PainterStageIn = MakeSRV(GraphBuilder, CompactStage);
			}
		}
		else
		{
			Parameters->VelocityOut = MakeUAV(GraphBuilder, Textures.Velocity[0]);
			Parameters->DensityOut = MakeUAV(GraphBuilder, Textures.Density[0]);
			Parameters->SurfaceDisplacementOut = MakeUAV(GraphBuilder, Textures.SurfaceDisplacement[0]);
			Parameters->PressureSolutionOut = MakeUAV(GraphBuilder, Textures.PressureSolution[0]);
			Parameters->TerrainElevationOut = MakeUAV(GraphBuilder, Textures.TerrainElevation);
			Parameters->ObstacleSdfOut = MakeUAV(GraphBuilder, Textures.ObstacleSdf);
			Parameters->HeatOut = MakeUAV(GraphBuilder, Textures.Heat[0]);
			if (CompactStage)
			{
				Parameters->VelocityStageIn = MakeSRV(GraphBuilder, Textures.Velocity[1]);
				Parameters->DensityStageIn = MakeSRV(GraphBuilder, Textures.Density[1]);
				Parameters->SurfaceDisplacementStageIn = MakeSRV(GraphBuilder, Textures.SurfaceDisplacement[1]);
				Parameters->PressureSolutionStageIn = MakeSRV(GraphBuilder, Textures.PressureSolution[1]);
				Parameters->TerrainStageIn = MakeSRV(GraphBuilder, CompactStage);
				Parameters->HeatStageIn = MakeSRV(GraphBuilder, Textures.Heat[1]);
			}
		}

		TShaderMapRef<FTDFluidGpuShiftCS> Shader(Context.ShaderMap, Permutation);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.ShiftScatter"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, ScatterCount));
	}

	void AddRasterizeObstaclesPass(const FTDFluidGpuBatchPassContext& Context)
	{
		const FTDFluidGpuBatchFrame& Frame = Context.Batch.GetFrame();
		if (Frame.ObstacleEntryIndices.IsEmpty())
		{
			return;
		}

		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FTDFluidGpuRasterizeObstaclesCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuRasterizeObstaclesCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->EntryList = UploadEntryList(GraphBuilder, TEXT("TDFluidGpu.ObstacleEntries"), TArray<uint32>(Frame.ObstacleEntryIndices));
		Parameters->Obstacles = Context.Obstacles;
		Parameters->ObstacleSdfOut = MakeUAV(GraphBuilder, Context.Textures.ObstacleSdf);

		TShaderMapRef<FTDFluidGpuRasterizeObstaclesCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.RasterizeObstacles"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, Frame.ObstacleEntryIndices.Num()));
	}

	void AddTerrainUpdatePass(const FTDFluidGpuBatchPassContext& Context)
	{
		TArray<FTDFluidGpuPackedTerrainUpdate> Updates = KeepLastTerrainUpdatePerCell(Context.Batch.GetFrame().TerrainUpdates);
		if (Updates.IsEmpty())
		{
			return;
		}

		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const int32 UpdateCount = Updates.Num();
		FTDFluidGpuTerrainUpdateCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuTerrainUpdateCS::FParameters>();
		Parameters->TerrainUpdates = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.TerrainUpdates"), MoveTemp(Updates)));
		Parameters->TerrainUpdateCount = static_cast<uint32>(UpdateCount);
		Parameters->TerrainElevationUpdateOut = MakeUAV(GraphBuilder, Context.Textures.TerrainElevation);

		TShaderMapRef<FTDFluidGpuTerrainUpdateCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.TerrainUpdate"), Shader, Parameters, FComputeShaderUtils::GetGroupCount(UpdateCount, TerrainUpdateGroupSize));
	}

	void AddApplyStampsPass(const FTDFluidGpuBatchPassContext& Context)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		FTDFluidGpuApplyStampsCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuApplyStampsCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->Stamps = Context.FrameResources.Stamps;
		Parameters->StampBinRanges = Context.FrameResources.StampBinRanges;
		Parameters->StampBinIndices = Context.FrameResources.StampBinIndices;
		Parameters->ObstacleSdf = MakeSRV(GraphBuilder, Textures.ObstacleSdf);
		Parameters->VelocityIn = MakeSRV(GraphBuilder, Textures.Velocity[0]);
		Parameters->DensityIn = MakeSRV(GraphBuilder, Textures.Density[0]);
		Parameters->SurfaceDisplacementIn = MakeSRV(GraphBuilder, Textures.SurfaceDisplacement[0]);
		Parameters->VelocityOut = MakeUAV(GraphBuilder, Textures.Velocity[1]);
		Parameters->DensityOut = MakeUAV(GraphBuilder, Textures.Density[1]);
		Parameters->SurfaceDisplacementOut = MakeUAV(GraphBuilder, Textures.SurfaceDisplacement[1]);
		Parameters->HeatIn = MakeSRV(GraphBuilder, Textures.Heat[0]);
		Parameters->HeatOut = MakeUAV(GraphBuilder, Textures.Heat[1]);

		TShaderMapRef<FTDFluidGpuApplyStampsCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.ApplyStamps"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, Context.EntryCount));
	}

	void AddForcesPass(const FTDFluidGpuBatchPassContext& Context)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		FTDFluidGpuForcesCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuForcesCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->ObstacleSdf = MakeSRV(GraphBuilder, Textures.ObstacleSdf);
		Parameters->TerrainElevation = MakeSRV(GraphBuilder, Textures.TerrainElevation);
		Parameters->VelocityIn = MakeSRV(GraphBuilder, Textures.Velocity[1]);
		Parameters->DensityIn = MakeSRV(GraphBuilder, Textures.Density[1]);
		Parameters->HeatIn = MakeSRV(GraphBuilder, Textures.Heat[1]);
		Parameters->VelocityOut = MakeUAV(GraphBuilder, Textures.Velocity[0]);

		TShaderMapRef<FTDFluidGpuForcesCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Forces"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, Context.EntryCount));
	}

	void AddVorticityPass(const FTDFluidGpuBatchPassContext& Context)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		FTDFluidGpuVorticityCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuVorticityCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->ObstacleSdf = MakeSRV(GraphBuilder, Textures.ObstacleSdf);
		Parameters->VelocityIn = MakeSRV(GraphBuilder, Textures.Velocity[0]);
		Parameters->VelocityOut = MakeUAV(GraphBuilder, Textures.Velocity[1]);

		TShaderMapRef<FTDFluidGpuVorticityCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Vorticity"), Shader, Parameters, MakeTileGroupCount(Context.Resolution, Context.EntryCount));
	}

	int32 AddJacobiPasses(const FTDFluidGpuBatchPassContext& Context, FRDGTextureRef ProjectedVelocity, int32 CurrentPressureIndex)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		const int32 PressureIterations = FMath::Max(Context.Batch.GetKey().PressureIterations, 1);
		const int32 DispatchCount = FMath::DivideAndRoundUp(PressureIterations, JacobiIterationsPerDispatch);
		FRDGTextureRef Divergence = GraphBuilder.CreateTexture(MakeStateArrayDesc(Context.Resolution, Context.Batch.StateTextures.SliceCapacity, DivergenceFormat), TEXT("TDFluidGpu.Divergence"));
		TShaderMapRef<FTDFluidGpuJacobiTileCS> DivergenceShader(Context.ShaderMap, FTDFluidGpuJacobiTileCS::FPermutationDomain(true));
		TShaderMapRef<FTDFluidGpuJacobiTileCS> RelaxShader(Context.ShaderMap, FTDFluidGpuJacobiTileCS::FPermutationDomain(false));

		for (int32 DispatchIndex = 0; DispatchIndex < DispatchCount; ++DispatchIndex)
		{
			const bool bComputesDivergence = DispatchIndex == 0;
			const int32 TargetPressureIndex = 1 - CurrentPressureIndex;
			FTDFluidGpuJacobiTileCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuJacobiTileCS::FParameters>();
			Parameters->ActorParameters = Context.ActorParameters;
			Parameters->ObstacleSdf = MakeSRV(GraphBuilder, Textures.ObstacleSdf);
			Parameters->PressureIn = MakeSRV(GraphBuilder, Textures.PressureSolution[CurrentPressureIndex]);
			Parameters->PressureOut = MakeUAV(GraphBuilder, Textures.PressureSolution[TargetPressureIndex]);
			Parameters->IterationCount = static_cast<uint32>(FMath::Min(JacobiIterationsPerDispatch, PressureIterations - DispatchIndex * JacobiIterationsPerDispatch));
			if (bComputesDivergence)
			{
				Parameters->VelocityIn = MakeSRV(GraphBuilder, ProjectedVelocity);
				Parameters->DivergenceOut = MakeUAV(GraphBuilder, Divergence);
			}
			else
			{
				Parameters->DivergenceIn = MakeSRV(GraphBuilder, Divergence);
			}

			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.JacobiTile"), bComputesDivergence ? DivergenceShader : RelaxShader, Parameters, MakeTileGroupCount(Context.Resolution, Context.EntryCount));
			CurrentPressureIndex = TargetPressureIndex;
		}
		return CurrentPressureIndex;
	}

	void AddSubtractGradientPass(const FTDFluidGpuBatchPassContext& Context, FRDGTextureRef Pressure, FRDGTextureRef VelocityIn, FRDGTextureRef VelocityOut, bool bEnforcesObstacleBoundaries)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FTDFluidGpuSubtractGradientCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuSubtractGradientCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->ObstacleSdf = MakeSRV(GraphBuilder, Context.Textures.ObstacleSdf);
		Parameters->PressureIn = MakeSRV(GraphBuilder, Pressure);
		Parameters->VelocityIn = MakeSRV(GraphBuilder, VelocityIn);
		Parameters->VelocityOut = MakeUAV(GraphBuilder, VelocityOut);
		Parameters->EnforcesObstacleBoundaries = bEnforcesObstacleBoundaries ? 1u : 0u;

		TShaderMapRef<FTDFluidGpuSubtractGradientCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.SubtractGradient"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, Context.EntryCount));
	}

	int32 ComputePressureMeanTilesPerRow(int32 Resolution)
	{
		return FMath::DivideAndRoundUp(Resolution, TileGroupSize);
	}

	FRDGBufferRef CreatePressurePartialSums(FRDGBuilder& GraphBuilder, int32 Resolution, int32 EntryCount)
	{
		const int32 TilesPerRow = ComputePressureMeanTilesPerRow(Resolution);
		return GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), static_cast<uint32>(TilesPerRow * TilesPerRow * EntryCount)), TEXT("TDFluidGpu.PressurePartialSums"));
	}

	void AddPressureMeanTilePass(const FTDFluidGpuBatchPassContext& Context, FRDGTextureRef Pressure, FRDGBufferRef PartialSums, int32 TilesPerRow)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FTDFluidGpuPressureMeanCS::FParameters* TileParameters = GraphBuilder.AllocParameters<FTDFluidGpuPressureMeanCS::FParameters>();
		TileParameters->ActorParameters = Context.ActorParameters;
		TileParameters->ObstacleSdf = MakeSRV(GraphBuilder, Context.Textures.ObstacleSdf);
		TileParameters->PressureIn = MakeSRV(GraphBuilder, Pressure);
		TileParameters->PressurePartialSumsOut = GraphBuilder.CreateUAV(PartialSums);
		TileParameters->TilesPerSlice = static_cast<uint32>(TilesPerRow * TilesPerRow);
		TileParameters->TilesPerRow = static_cast<uint32>(TilesPerRow);
		TShaderMapRef<FTDFluidGpuPressureMeanCS> TileShader(Context.ShaderMap, FTDFluidGpuPressureMeanCS::FPermutationDomain(0));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.PressureMeanTiles"), TileShader, TileParameters, MakeTileGroupCount(Context.Resolution, Context.EntryCount));
	}

	FRDGBufferRef AddPressureMeanPasses(const FTDFluidGpuBatchPassContext& Context, FRDGTextureRef Pressure, FRDGBufferRef PrecomputedPartialSums)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const int32 TilesPerRow = ComputePressureMeanTilesPerRow(Context.Resolution);
		const int32 TilesPerSlice = TilesPerRow * TilesPerRow;
		FRDGBufferRef MeanPressure = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), static_cast<uint32>(Context.EntryCount)), TEXT("TDFluidGpu.MeanPressure"));
		FRDGBufferRef PartialSums = PrecomputedPartialSums;
		if (PartialSums == nullptr)
		{
			PartialSums = CreatePressurePartialSums(GraphBuilder, Context.Resolution, Context.EntryCount);
			AddPressureMeanTilePass(Context, Pressure, PartialSums, TilesPerRow);
		}

		FTDFluidGpuPressureMeanCS::FParameters* FinalParameters = GraphBuilder.AllocParameters<FTDFluidGpuPressureMeanCS::FParameters>();
		FinalParameters->PressurePartialSumsIn = GraphBuilder.CreateSRV(PartialSums);
		FinalParameters->MeanPressureOut = GraphBuilder.CreateUAV(MeanPressure);
		FinalParameters->TilesPerSlice = static_cast<uint32>(TilesPerSlice);
		FinalParameters->TilesPerRow = static_cast<uint32>(TilesPerRow);
		TShaderMapRef<FTDFluidGpuPressureMeanCS> FinalShader(Context.ShaderMap, FTDFluidGpuPressureMeanCS::FPermutationDomain(1));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.PressureMeanFinal"), FinalShader, FinalParameters, FIntVector(1, 1, Context.EntryCount));
		return MeanPressure;
	}

	struct FTDFluidGpuMultigridLevelTextures
	{
		FRDGTextureRef Rhs = nullptr;
		FRDGTextureRef Apertures = nullptr;
		FRDGTextureRef Correction[2] = { nullptr, nullptr };
		FRDGTextureRef ComponentLabels = nullptr;
	};

	using FTDFluidGpuMultigridLevels = TArray<FTDFluidGpuMultigridLevelTextures, TInlineAllocator<8>>;

	struct FTDFluidGpuMultigridPassContext
	{
		FRDGBuilder& GraphBuilder;
		FGlobalShaderMap* ShaderMap = nullptr;
		FRDGBufferSRVRef ActorParameters = nullptr;
		FRDGTextureRef ObstacleSdf = nullptr;
		int32 Resolution = 0;
		int32 EntryCount = 0;
		FTDFluidGpuPassTimestamps* PassTimestamps = nullptr;
	};

	bool IsMultigridBatch(const FTDFluidGpuBatch& Batch)
	{
		return Batch.GetKey().PressureSolver == ETDFluidGpuPressureSolver::Multigrid;
	}

	float ComputeMultigridLevelWeightScale(int32 Level)
	{
		return 1.0f / static_cast<float>(1 << (2 * Level));
	}

	FTDFluidGpuMultigridLevels CreateMultigridLevels(const FTDFluidGpuMultigridPassContext& Context)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FTDFluidGpuMultigridLevels Levels;
		const int32 LevelCount = FTDFluidGpuBatch::ComputeMultigridLevelCount(Context.Resolution);
		Levels.SetNum(LevelCount);
		for (int32 Level = 1; Level < LevelCount; ++Level)
		{
			const int32 LevelResolution = Context.Resolution >> Level;
			FTDFluidGpuMultigridLevelTextures& LevelTextures = Levels[Level];
			LevelTextures.Rhs = GraphBuilder.CreateTexture(MakeStateArrayDesc(LevelResolution, Context.EntryCount, MultigridValueFormat), TEXT("TDFluidGpu.MultigridRhs"));
			LevelTextures.Apertures = GraphBuilder.CreateTexture(MakeStateArrayDesc(LevelResolution, Context.EntryCount, MultigridApertureFormat), TEXT("TDFluidGpu.MultigridApertures"));
			LevelTextures.Correction[0] = GraphBuilder.CreateTexture(MakeStateArrayDesc(LevelResolution, Context.EntryCount, MultigridValueFormat), TEXT("TDFluidGpu.MultigridCorrection"));
			if (Level < LevelCount - 1)
			{
				LevelTextures.Correction[1] = GraphBuilder.CreateTexture(MakeStateArrayDesc(LevelResolution, Context.EntryCount, MultigridValueFormat), TEXT("TDFluidGpu.MultigridSmoothedCorrection"));
			}
			else
			{
				LevelTextures.ComponentLabels = GraphBuilder.CreateTexture(MakeStateArrayDesc(LevelResolution, Context.EntryCount, MultigridComponentLabelFormat), TEXT("TDFluidGpu.MultigridComponentLabels"));
			}
		}
		return Levels;
	}

	void AddMultigridDownPass(const FTDFluidGpuMultigridPassContext& Context, const FTDFluidGpuMultigridLevels& Levels, int32 Level, FRDGTextureRef ProjectedVelocity, FRDGTextureRef Pressure, FRDGTextureRef SmoothedPressure, bool bWritesCoarseApertures, float WarmStartScale)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const bool bIsFinestLevel = Level == 0;
		const int32 LevelResolution = Context.Resolution >> Level;
		const FTDFluidGpuMultigridLevelTextures& CoarseLevel = Levels[Level + 1];
		FTDFluidGpuMultigridDownCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuMultigridDownCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->CoarseRhsOut = MakeUAV(GraphBuilder, CoarseLevel.Rhs);
		Parameters->LevelWeightScale = ComputeMultigridLevelWeightScale(Level);
		Parameters->WarmStartScale = bIsFinestLevel ? WarmStartScale : 1.0f;
		Parameters->LevelResolution = LevelResolution;
		if (bWritesCoarseApertures)
		{
			Parameters->CoarseAperturesOut = MakeUAV(GraphBuilder, CoarseLevel.Apertures);
		}
		if (bIsFinestLevel)
		{
			Parameters->ObstacleSdf = MakeSRV(GraphBuilder, Context.ObstacleSdf);
			Parameters->VelocityIn = MakeSRV(GraphBuilder, ProjectedVelocity);
			Parameters->ValueIn = MakeSRV(GraphBuilder, Pressure);
			Parameters->ValueOut = MakeUAV(GraphBuilder, SmoothedPressure);
		}
		else
		{
			const FTDFluidGpuMultigridLevelTextures& FineLevel = Levels[Level];
			Parameters->RhsIn = MakeSRV(GraphBuilder, FineLevel.Rhs);
			Parameters->AperturesIn = MakeSRV(GraphBuilder, FineLevel.Apertures);
			Parameters->ValueOut = MakeUAV(GraphBuilder, FineLevel.Correction[0]);
		}

		FTDFluidGpuMultigridDownCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTDFluidGpuMultigridDownCS::FFinestLevelDimension>(bIsFinestLevel);
		PermutationVector.Set<FTDFluidGpuMultigridDownCS::FWritesCoarseAperturesDimension>(bWritesCoarseApertures);
		TShaderMapRef<FTDFluidGpuMultigridDownCS> Shader(Context.ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.MultigridDown L%d", Level), Shader, Parameters, MakeTileGroupCount(LevelResolution, Context.EntryCount));
	}

	void AddMultigridCoarseSolvePass(const FTDFluidGpuMultigridPassContext& Context, const FTDFluidGpuMultigridLevels& Levels, bool bComputesComponentLabels)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const int32 CoarsestLevel = Levels.Num() - 1;
		const FTDFluidGpuMultigridLevelTextures& Coarsest = Levels[CoarsestLevel];
		FTDFluidGpuMultigridCoarseSolveCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuMultigridCoarseSolveCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->RhsIn = MakeSRV(GraphBuilder, Coarsest.Rhs);
		Parameters->AperturesIn = MakeSRV(GraphBuilder, Coarsest.Apertures);
		Parameters->CorrectionOut = MakeUAV(GraphBuilder, Coarsest.Correction[0]);
		Parameters->ComponentLabels = MakeUAV(GraphBuilder, Coarsest.ComponentLabels);
		Parameters->LevelWeightScale = ComputeMultigridLevelWeightScale(CoarsestLevel);
		Parameters->ComputesComponentLabels = bComputesComponentLabels ? 1u : 0u;

		TShaderMapRef<FTDFluidGpuMultigridCoarseSolveCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.MultigridCoarseSolve"), Shader, Parameters, FIntVector(1, 1, Context.EntryCount));
	}

	void AddMultigridUpPass(const FTDFluidGpuMultigridPassContext& Context, const FTDFluidGpuMultigridLevels& Levels, int32 Level, FRDGTextureRef ProjectedVelocity, FRDGTextureRef SmoothedPressure, FRDGTextureRef Pressure, FRDGBufferRef PressurePartialSums)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const bool bIsFinestLevel = Level == 0;
		const int32 LevelResolution = Context.Resolution >> Level;
		const bool bIsAboveCoarsest = Level + 1 == Levels.Num() - 1;
		const FTDFluidGpuMultigridLevelTextures& CoarseLevel = Levels[Level + 1];
		FTDFluidGpuMultigridUpCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuMultigridUpCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->CoarseCorrectionIn = MakeSRV(GraphBuilder, bIsAboveCoarsest ? CoarseLevel.Correction[0] : CoarseLevel.Correction[1]);
		Parameters->CoarseAperturesIn = MakeSRV(GraphBuilder, CoarseLevel.Apertures);
		Parameters->LevelWeightScale = ComputeMultigridLevelWeightScale(Level);
		Parameters->LevelResolution = LevelResolution;
		if (bIsFinestLevel)
		{
			Parameters->ObstacleSdf = MakeSRV(GraphBuilder, Context.ObstacleSdf);
			Parameters->VelocityIn = MakeSRV(GraphBuilder, ProjectedVelocity);
			Parameters->ValueIn = MakeSRV(GraphBuilder, SmoothedPressure);
			Parameters->ValueOut = MakeUAV(GraphBuilder, Pressure);
		}
		else
		{
			const FTDFluidGpuMultigridLevelTextures& FineLevel = Levels[Level];
			Parameters->RhsIn = MakeSRV(GraphBuilder, FineLevel.Rhs);
			Parameters->AperturesIn = MakeSRV(GraphBuilder, FineLevel.Apertures);
			Parameters->ValueIn = MakeSRV(GraphBuilder, FineLevel.Correction[0]);
			Parameters->ValueOut = MakeUAV(GraphBuilder, FineLevel.Correction[1]);
		}

		const bool bWritesPressurePartialSums = bIsFinestLevel && PressurePartialSums != nullptr;
		if (bWritesPressurePartialSums)
		{
			const int32 TilesPerRow = ComputePressureMeanTilesPerRow(Context.Resolution);
			Parameters->PressurePartialSumsOut = GraphBuilder.CreateUAV(PressurePartialSums);
			Parameters->TilesPerSlice = static_cast<uint32>(TilesPerRow * TilesPerRow);
			Parameters->TilesPerRow = static_cast<uint32>(TilesPerRow);
		}

		FTDFluidGpuMultigridUpCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTDFluidGpuMultigridUpCS::FFinestLevelDimension>(bIsFinestLevel);
		PermutationVector.Set<FTDFluidGpuMultigridUpCS::FWritesPressurePartialSumsDimension>(bWritesPressurePartialSums);
		TShaderMapRef<FTDFluidGpuMultigridUpCS> Shader(Context.ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.MultigridUp L%d", Level), Shader, Parameters, MakeTileGroupCount(LevelResolution, Context.EntryCount));
	}

	void AddMultigridCyclePasses(const FTDFluidGpuMultigridPassContext& Context, const FTDFluidGpuMultigridLevels& Levels, FRDGTextureRef ProjectedVelocity, FRDGTextureRef Pressure, FRDGTextureRef SmoothedPressure, bool bWritesCoarseApertures, float WarmStartScale, FRDGBufferRef PressurePartialSums)
	{
		const int32 CoarsestLevel = Levels.Num() - 1;
		for (int32 Level = 0; Level < CoarsestLevel; ++Level)
		{
			AddMultigridDownPass(Context, Levels, Level, ProjectedVelocity, Pressure, SmoothedPressure, bWritesCoarseApertures, WarmStartScale);
			AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("mg_down_L"), Level);
		}

		AddMultigridCoarseSolvePass(Context, Levels, bWritesCoarseApertures);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("mg_coarse"));
		for (int32 Level = CoarsestLevel - 1; Level >= 0; --Level)
		{
			AddMultigridUpPass(Context, Levels, Level, ProjectedVelocity, SmoothedPressure, Pressure, PressurePartialSums);
			AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("mg_up_L"), Level);
		}
	}

	TAutoConsoleVariable<int32> CVarTDFluidGpuPressureWarmStartDecayFrames(
		TEXT("TD.FluidFX.GpuPressureWarmStartDecayFrames"),
		TDFluidGpuSolverConstants::DefaultMultigridWarmStartDecayFrames,
		TEXT("Multigrid GPU fluid steps scale the warm start pressure by 1 - 1/N once per step, which bounds the constant offset that incompatible divergence builds up in closed pockets. 0 keeps the full warm start."),
		ECVF_RenderThreadSafe);

	float ComputeMultigridWarmStartScale()
	{
		const int32 DecayFrames = CVarTDFluidGpuPressureWarmStartDecayFrames.GetValueOnAnyThread();
		return DecayFrames > 0 ? 1.0f - 1.0f / static_cast<float>(DecayFrames) : 1.0f;
	}

	FTDFluidGpuMultigridPassContext MakeMultigridPassContext(const FTDFluidGpuBatchPassContext& Context)
	{
		return FTDFluidGpuMultigridPassContext{ Context.GraphBuilder, Context.ShaderMap, Context.ActorParameters, Context.Textures.ObstacleSdf, Context.Resolution, Context.EntryCount, Context.PassTimestamps };
	}

	int32 AddPressureSolvePasses(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuMultigridLevels& MultigridLevels, FRDGTextureRef ProjectedVelocity, int32 PressureIndex, bool bIsFirstProjection, FRDGBufferRef PressurePartialSums)
	{
		if (!IsMultigridBatch(Context.Batch))
		{
			const int32 SolvedPressureIndex = AddJacobiPasses(Context, ProjectedVelocity, PressureIndex);
			AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("jacobi"));
			return SolvedPressureIndex;
		}

		const FTDFluidGpuMultigridPassContext MultigridContext = MakeMultigridPassContext(Context);
		FRDGTextureRef Pressure = Context.Textures.PressureSolution[PressureIndex];
		FRDGTextureRef SmoothedPressure = Context.Textures.PressureSolution[1 - PressureIndex];
		const int32 CycleCount = FMath::Max(Context.Batch.GetKey().MultigridCycles, 1);
		const float FirstCycleWarmStartScale = bIsFirstProjection ? ComputeMultigridWarmStartScale() : 1.0f;
		for (int32 CycleIndex = 0; CycleIndex < CycleCount; ++CycleIndex)
		{
			const bool bIsLastCycle = CycleIndex == CycleCount - 1;
			AddMultigridCyclePasses(MultigridContext, MultigridLevels, ProjectedVelocity, Pressure, SmoothedPressure, bIsFirstProjection && CycleIndex == 0, CycleIndex == 0 ? FirstCycleWarmStartScale : 1.0f, bIsLastCycle ? PressurePartialSums : nullptr);
		}
		return PressureIndex;
	}

	void AddAdvectVelocityPass(const FTDFluidGpuBatchPassContext& Context)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		FTDFluidGpuAdvectVelocityCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuAdvectVelocityCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->ObstacleSdf = MakeSRV(GraphBuilder, Textures.ObstacleSdf);
		Parameters->VelocityIn = MakeSRV(GraphBuilder, Textures.Velocity[0]);
		Parameters->VelocityOut = MakeUAV(GraphBuilder, Textures.Velocity[1]);

		TShaderMapRef<FTDFluidGpuAdvectVelocityCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.AdvectVelocity"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, Context.EntryCount));
	}

	void AddAdvectDensityPackPasses(const FTDFluidGpuBatchPassContext& Context, FRDGTextureRef Pressure, FRDGBufferRef MeanPressure, FTDFluidGpuFrameOutputs& InOutOutputs, TArray<FTDFluidGpuEntryDensityOutput>& OutDensityOutputs)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		const FTDFluidGpuBatchFrame& Frame = Context.Batch.GetFrame();
		FRDGBufferSRVRef MeanPressureSRV = GraphBuilder.CreateSRV(MeanPressure);
		FRDGTextureSRVRef ObstacleSdfSRV = MakeSRV(GraphBuilder, Textures.ObstacleSdf);
		FRDGTextureSRVRef TerrainElevationSRV = MakeSRV(GraphBuilder, Textures.TerrainElevation);
		FRDGTextureSRVRef VelocitySRV = MakeSRV(GraphBuilder, Textures.Velocity[0]);
		FRDGTextureSRVRef DensitySRV = MakeSRV(GraphBuilder, Textures.Density[1]);
		FRDGTextureSRVRef SurfaceDisplacementSRV = MakeSRV(GraphBuilder, Textures.SurfaceDisplacement[1]);
		FRDGTextureUAVRef DensityUAV = MakeUnorderedUAV(GraphBuilder, Textures.Density[0]);
		FRDGTextureUAVRef SurfaceDisplacementUAV = MakeUnorderedUAV(GraphBuilder, Textures.SurfaceDisplacement[0]);
		FRDGTextureUAVRef PressureUAV = MakeUnorderedUAV(GraphBuilder, Pressure);
		FRDGTextureSRVRef HeatSRV = MakeSRV(GraphBuilder, Textures.Heat[1]);
		FRDGTextureUAVRef HeatUAV = MakeUnorderedUAV(GraphBuilder, Textures.Heat[0]);
		FRDGTextureUAVRef PlaceholderAuxUAV = nullptr;

		for (int32 EntryIndex = 0; EntryIndex < Context.EntryCount; ++EntryIndex)
		{
			const FTDFluidGpuActorInput& Actor = Context.FrameInputs.Actors[Frame.Entries[EntryIndex].ActorInputIndex];
			const FTDFluidGpuOutputTargets Targets = RegisterOutputTargets(GraphBuilder, Actor, Context.Resolution, true);
			const bool bWritesOutput = Targets.DensityPressure != nullptr;

			FTDFluidGpuAdvectDensityPackCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuAdvectDensityPackCS::FParameters>();
			Parameters->ActorParameters = Context.ActorParameters;
			Parameters->Stamps = Context.FrameResources.Stamps;
			Parameters->StampBinRanges = Context.FrameResources.StampBinRanges;
			Parameters->StampBinIndices = Context.FrameResources.StampBinIndices;
			Parameters->MeanPressure = MeanPressureSRV;
			Parameters->ObstacleSdf = ObstacleSdfSRV;
			Parameters->TerrainElevation = TerrainElevationSRV;
			Parameters->VelocityIn = VelocitySRV;
			Parameters->DensityIn = DensitySRV;
			Parameters->SurfaceDisplacementIn = SurfaceDisplacementSRV;
			Parameters->DensityOut = DensityUAV;
			Parameters->SurfaceDisplacementOut = SurfaceDisplacementUAV;
			Parameters->PressureSolution = PressureUAV;
			Parameters->HeatIn = HeatSRV;
			Parameters->HeatOut = HeatUAV;
			Parameters->EntryIndex = static_cast<uint32>(EntryIndex);
			if (bWritesOutput)
			{
				Parameters->DensityPressureOutput = MakeUAV(GraphBuilder, Targets.DensityPressure);
				Parameters->VelocityOutput = MakeUAV(GraphBuilder, Targets.Velocity);
				FRDGTextureRef AuxTarget = Context.Batch.GetKey().bUsesHeatChannel ? RegisterOutputTexture(GraphBuilder, Actor.AuxOutput, Context.Resolution, TEXT("TDFluidGpu.AuxOutput")) : nullptr;
				if (AuxTarget)
				{
					Parameters->AuxOutput = MakeUAV(GraphBuilder, AuxTarget);
				}
				else
				{
					if (!PlaceholderAuxUAV)
					{
						PlaceholderAuxUAV = MakeUAV(GraphBuilder, GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(1, 1), OutputFormat, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV), TEXT("TDFluidGpu.AuxPlaceholder")));
					}
					Parameters->AuxOutput = PlaceholderAuxUAV;
				}
				FTDFluidGpuEntryDensityOutput& DensityOutput = OutDensityOutputs.AddDefaulted_GetRef();
				DensityOutput.EntryIndex = EntryIndex;
				DensityOutput.DensityPressure = Targets.DensityPressure;
				++InOutOutputs.WrittenOutputCount;
			}
			else
			{
				++InOutOutputs.SkippedOutputCount;
			}

			TShaderMapRef<FTDFluidGpuAdvectDensityPackCS> Shader(Context.ShaderMap, FTDFluidGpuAdvectDensityPackCS::FPermutationDomain(bWritesOutput));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.AdvectDensityPack"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, 1));
		}
	}

	bool IsTransmittanceRequested(const FTDFluidGpuActorInput& Actor)
	{
		return Actor.TransmittanceOutput != nullptr && Actor.Transmittance.bIsEnabled && Actor.Transmittance.LightMode > 0;
	}

	void AddTransmittancePasses(const FTDFluidGpuBatchPassContext& Context, TConstArrayView<FTDFluidGpuEntryDensityOutput> DensityOutputs)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchFrame& Frame = Context.Batch.GetFrame();
		const int32 OutputResolution = FTDFluidGpuBatch::ComputeTransmittanceResolution(Context.Resolution);
		for (const FTDFluidGpuEntryDensityOutput& DensityOutput : DensityOutputs)
		{
			const FTDFluidGpuActorParameters& ActorParameters = Frame.Parameters[DensityOutput.EntryIndex];
			const FTDFluidGpuActorInput& Actor = Context.FrameInputs.Actors[Frame.Entries[DensityOutput.EntryIndex].ActorInputIndex];
			if (!IsTransmittanceRequested(Actor))
			{
				continue;
			}

			FRDGTextureRef TransmittanceTarget = RegisterSizedOutputTexture(GraphBuilder, Actor.TransmittanceOutput, OutputResolution, TEXT("TDFluidGpu.TransmittanceOutput"));
			if (!TransmittanceTarget)
			{
				continue;
			}

			const FTDFluidGpuTransmittanceInput& Input = Actor.Transmittance;
			FTDFluidGpuTransmittanceCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuTransmittanceCS::FParameters>();
			Parameters->DensityPressureTexture = DensityOutput.DensityPressure;
			Parameters->TransmittanceOutput = MakeUAV(GraphBuilder, TransmittanceTarget);
			Parameters->VolumeBoxSizeCm = FVector4f(ActorParameters.ExtentCm.X, ActorParameters.ExtentCm.Y, FMath::Max(Input.VolumeHeightCm, 1.0f), 0.0f);
			Parameters->LightPositionLocalCm = FVector4f(Input.LightPositionLocalCm, 0.0f);
			Parameters->LightDirectionLocal = FVector4f(Input.LightDirectionLocal, 0.0f);
			Parameters->SimResolution = Context.Resolution;
			Parameters->OutputResolution = OutputResolution;
			Parameters->MarchStepCount = FMath::Clamp(Input.MarchStepCount, TDFluidGpuSolverConstants::MinimumTransmittanceMarchSteps, TDFluidGpuSolverConstants::MaximumTransmittanceMarchSteps);
			Parameters->LightMode = Input.LightMode;
			Parameters->IsSymmetric = Input.bIsSymmetric ? 1u : 0u;
			Parameters->DensityScale = Input.DensityScale;
			Parameters->SoftFalloffCm = Input.SoftFalloffCm;

			TShaderMapRef<FTDFluidGpuTransmittanceCS> Shader(Context.ShaderMap);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Transmittance"), Shader, Parameters, FComputeShaderUtils::GetGroupCount(FIntPoint(OutputResolution, OutputResolution), TransmittanceGroupSize));
		}
	}

	void AddPainterStepPasses(const FTDFluidGpuBatchPassContext& Context, FTDFluidGpuFrameOutputs& InOutOutputs)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchFrame& Frame = Context.Batch.GetFrame();
		FRDGTextureUAVRef PrimaryUAV = MakeUnorderedUAV(GraphBuilder, Context.Textures.PainterPrimaryDensity);
		FRDGTextureUAVRef SelectiveUAV = MakeUnorderedUAV(GraphBuilder, Context.Textures.PainterSelectiveDensity);

		for (int32 EntryIndex = 0; EntryIndex < Context.EntryCount; ++EntryIndex)
		{
			const FTDFluidGpuActorInput& Actor = Context.FrameInputs.Actors[Frame.Entries[EntryIndex].ActorInputIndex];
			const FTDFluidGpuOutputTargets Targets = RegisterOutputTargets(GraphBuilder, Actor, Context.Resolution, false);
			const bool bWritesOutput = Targets.DensityPressure != nullptr;

			FTDFluidGpuPainterStepCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuPainterStepCS::FParameters>();
			Parameters->ActorParameters = Context.ActorParameters;
			Parameters->Stamps = Context.FrameResources.Stamps;
			Parameters->StampBinRanges = Context.FrameResources.StampBinRanges;
			Parameters->StampBinIndices = Context.FrameResources.StampBinIndices;
			Parameters->Obstacles = Context.Obstacles;
			Parameters->PainterPrimaryDensity = PrimaryUAV;
			Parameters->PainterSelectiveDensity = SelectiveUAV;
			Parameters->EntryIndex = static_cast<uint32>(EntryIndex);
			if (bWritesOutput)
			{
				Parameters->DensityPressureOutput = MakeUAV(GraphBuilder, Targets.DensityPressure);
				++InOutOutputs.WrittenOutputCount;
			}
			else
			{
				++InOutOutputs.SkippedOutputCount;
			}

			TShaderMapRef<FTDFluidGpuPainterStepCS> Shader(Context.ShaderMap, FTDFluidGpuPainterStepCS::FPermutationDomain(bWritesOutput));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.PainterStep"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, 1));
		}
	}

	void AddReadbackDownsamplePass(const FTDFluidGpuBatchPassContext& Context)
	{
		if (!Context.FrameResources.ReadbackAtlasUAV)
		{
			return;
		}

		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const bool bIsPainter = IsPainterBatch(Context.Batch);
		FTDFluidGpuReadbackDownsampleCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuReadbackDownsampleCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->ReadbackAtlasOut = Context.FrameResources.ReadbackAtlasUAV;
		if (bIsPainter)
		{
			Parameters->PainterPrimaryDensityIn = MakeSRV(GraphBuilder, Context.Textures.PainterPrimaryDensity);
			Parameters->PainterSelectiveDensityIn = MakeSRV(GraphBuilder, Context.Textures.PainterSelectiveDensity);
		}
		else
		{
			Parameters->VelocityIn = MakeSRV(GraphBuilder, Context.Textures.Velocity[0]);
			Parameters->DensityIn = MakeSRV(GraphBuilder, Context.Textures.Density[0]);
		}

		TShaderMapRef<FTDFluidGpuReadbackDownsampleCS> Shader(Context.ShaderMap, FTDFluidGpuReadbackDownsampleCS::FPermutationDomain(bIsPainter));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.ReadbackDownsample"), Shader, Parameters, MakeCellGroupCount(TDFluidGpuSolverConstants::ReadbackTileResolution, Context.EntryCount));
	}

	void AddFluidSimPasses(const FTDFluidGpuBatchPassContext& Context, FTDFluidGpuFrameOutputs& InOutOutputs)
	{
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("batch_setup"));
		AddShiftAndResetPasses(Context);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("shift_reset"));
		AddRasterizeObstaclesPass(Context);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("rasterize_obstacles"));
		AddTerrainUpdatePass(Context);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("terrain_update"));
		AddApplyStampsPass(Context);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("apply_stamps"));
		AddForcesPass(Context);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("forces"));
		AddVorticityPass(Context);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("vorticity"));

		const FTDFluidGpuMultigridLevels MultigridLevels = IsMultigridBatch(Context.Batch) ? CreateMultigridLevels(MakeMultigridPassContext(Context)) : FTDFluidGpuMultigridLevels();
		int32 PressureIndex = AddPressureSolvePasses(Context, MultigridLevels, Textures.Velocity[1], 0, true, nullptr);
		AddSubtractGradientPass(Context, Textures.PressureSolution[PressureIndex], Textures.Velocity[1], Textures.Velocity[0], false);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("subtract_gradient"));
		AddAdvectVelocityPass(Context);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("advect_velocity"));
		FRDGBufferRef MultigridPressurePartialSums = IsMultigridBatch(Context.Batch) ? CreatePressurePartialSums(Context.GraphBuilder, Context.Resolution, Context.EntryCount) : nullptr;
		PressureIndex = AddPressureSolvePasses(Context, MultigridLevels, Textures.Velocity[1], PressureIndex, false, MultigridPressurePartialSums);
		FRDGBufferRef MeanPressure = AddPressureMeanPasses(Context, Textures.PressureSolution[PressureIndex], MultigridPressurePartialSums);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("pressure_mean"));
		AddSubtractGradientPass(Context, Textures.PressureSolution[PressureIndex], Textures.Velocity[1], Textures.Velocity[0], true);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("subtract_gradient"));
		TArray<FTDFluidGpuEntryDensityOutput> DensityOutputs;
		AddAdvectDensityPackPasses(Context, Textures.PressureSolution[PressureIndex], MeanPressure, InOutOutputs, DensityOutputs);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("advect_density_pack"));
		AddTransmittancePasses(Context, DensityOutputs);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("transmittance"));
		AddReadbackDownsamplePass(Context);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("readback_downsample"));
	}

	void AddSimplePainterPasses(const FTDFluidGpuBatchPassContext& Context, FTDFluidGpuFrameOutputs& InOutOutputs)
	{
		AddShiftAndResetPasses(Context);
		AddPainterStepPasses(Context, InOutOutputs);
		AddReadbackDownsamplePass(Context);
	}

	void AddBatchPasses(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const FTDFluidGpuFrameInputs& FrameInputs, const FTDFluidGpuFrameResources& FrameResources, FTDFluidGpuBatch& Batch, FTDFluidGpuFrameOutputs& InOutOutputs, FTDFluidGpuPassTimestamps* PassTimestamps)
	{
		const FTDFluidGpuBatchFrame& Frame = Batch.GetFrame();
		if (Frame.Entries.IsEmpty())
		{
			return;
		}

		RDG_EVENT_SCOPE(GraphBuilder, "TDFluidGpuBatch %dx%d x%d", Batch.GetKey().ResolutionTier, Batch.GetKey().ResolutionTier, Frame.Entries.Num());
		const FTDFluidGpuBatchGraphTextures Textures = RegisterBatchStateTextures(GraphBuilder, Batch);
		FRDGBufferSRVRef ActorParameters = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.ActorParameters"), Frame.Parameters));
		FRDGBufferSRVRef Obstacles = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Obstacles"), Frame.Obstacles));
		const FTDFluidGpuBatchPassContext Context(GraphBuilder, ShaderMap, FrameInputs, FrameResources, Batch, Textures, ActorParameters, Obstacles, PassTimestamps);

		InOutOutputs.SteppedEntryCount += Context.EntryCount;
		if (IsPainterBatch(Batch))
		{
			AddSimplePainterPasses(Context, InOutOutputs);
			return;
		}

		AddFluidSimPasses(Context, InOutOutputs);
	}

	int64 ComputePooledTextureBytes(const TRefCountPtr<IPooledRenderTarget>& PooledTexture)
	{
		if (!PooledTexture.IsValid())
		{
			return 0;
		}

		const FPooledRenderTargetDesc& Desc = PooledTexture->GetDesc();
		const int64 BlockBytes = GPixelFormats[Desc.Format].BlockBytes;
		return static_cast<int64>(Desc.Extent.X) * Desc.Extent.Y * FMath::Max<int64>(Desc.ArraySize, 1) * BlockBytes;
	}
}

namespace
{
	FTDFluidGpuFrameOutputs AddFramePasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuFrameInputs& FrameInputs, TArray<FTDFluidGpuBatch>& InOutBatches, FTDFluidGpuPassTimestamps* PassTimestamps)
	{
		FTDFluidGpuPreparedFrame PreparedFrame;
		FTDFluidGpuBatch::PrepareFrame(FrameInputs, InOutBatches, PreparedFrame);

		FTDFluidGpuFrameOutputs Outputs;
		Outputs.FrameNumber = FrameInputs.FrameNumber;
		Outputs.ReadbackLayout = PreparedFrame.ReadbackLayout;

		const bool bHasEntries = InOutBatches.ContainsByPredicate([](const FTDFluidGpuBatch& Batch)
		{
			return !Batch.GetFrame().Entries.IsEmpty();
		});
		if (!bHasEntries)
		{
			return Outputs;
		}

		RDG_EVENT_SCOPE_STAT(GraphBuilder, TDFluidGpuSolver, "TDFluidGpuStep");
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		AddPassTimestamp(GraphBuilder, PassTimestamps, TEXT("graph_begin"));
		const FTDFluidGpuFrameResources FrameResources = CreateFrameResources(GraphBuilder, PreparedFrame);
		AddPassTimestamp(GraphBuilder, PassTimestamps, TEXT("frame_resources"));
		for (FTDFluidGpuBatch& Batch : InOutBatches)
		{
			AddBatchPasses(GraphBuilder, ShaderMap, FrameInputs, FrameResources, Batch, Outputs, PassTimestamps);
		}

		Outputs.ReadbackAtlas = FrameResources.ReadbackAtlas;
		return Outputs;
	}
}

FTDFluidGpuFrameOutputs AddTDFluidGpuFramePasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuFrameInputs& FrameInputs, TArray<FTDFluidGpuBatch>& InOutBatches)
{
	return AddFramePasses(GraphBuilder, FrameInputs, InOutBatches, nullptr);
}

FTDFluidGpuFrameOutputs AddTDFluidGpuTimestampedFramePasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuFrameInputs& FrameInputs, TArray<FTDFluidGpuBatch>& InOutBatches, FTDFluidGpuPassTimestamps& OutTimestamps)
{
	return AddFramePasses(GraphBuilder, FrameInputs, InOutBatches, &OutTimestamps);
}

void AddTDFluidGpuMultigridProbePasses(FRDGBuilder& GraphBuilder, FTDFluidGpuBatch& Batch, int32 EntryIndex, const TArray<int32>& RecordedCycleCounts, FTDFluidGpuMultigridProbeReadbacks& OutReadbacks)
{
	const FTDFluidGpuBatchFrame& Frame = Batch.GetFrame();
	FTDFluidGpuBatchStateTextures& State = Batch.StateTextures;
	if (!Frame.Parameters.IsValidIndex(EntryIndex) || !State.Velocity[1].IsValid() || !State.ObstacleSdf.IsValid())
	{
		return;
	}

	const int32 Resolution = Batch.GetKey().ResolutionTier;
	const uint32 SliceIndex = static_cast<uint32>(Frame.Parameters[EntryIndex].SliceIndex);
	FRDGTextureRef Velocity = GraphBuilder.RegisterExternalTexture(State.Velocity[1], TEXT("TDFluidGpu.ProbeVelocity"));
	FRDGTextureRef ObstacleSdf = GraphBuilder.RegisterExternalTexture(State.ObstacleSdf, TEXT("TDFluidGpu.ProbeObstacleSdf"));
	FRDGTextureRef Pressure = GraphBuilder.CreateTexture(MakeStateArrayDesc(Resolution, State.SliceCapacity, PressureSolutionFormat), TEXT("TDFluidGpu.ProbePressure"));
	FRDGTextureRef SmoothedPressure = GraphBuilder.CreateTexture(MakeStateArrayDesc(Resolution, State.SliceCapacity, PressureSolutionFormat), TEXT("TDFluidGpu.ProbeSmoothedPressure"));
	AddClearUAVPass(GraphBuilder, MakeUAV(GraphBuilder, Pressure), 0.0f);

	FRDGBufferSRVRef ActorParameters = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.ProbeActorParameters"), Frame.Parameters));
	const FTDFluidGpuMultigridPassContext Context{ GraphBuilder, GetGlobalShaderMap(GMaxRHIFeatureLevel), ActorParameters, ObstacleSdf, Resolution, Frame.Parameters.Num() };
	const FTDFluidGpuMultigridLevels Levels = CreateMultigridLevels(Context);

	OutReadbacks.SliceIndex = SliceIndex;
	OutReadbacks.Velocity = MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.ProbeVelocityReadback"));
	OutReadbacks.ObstacleSdf = MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.ProbeObstacleSdfReadback"));
	AddEnqueueCopyPass(GraphBuilder, OutReadbacks.Velocity.Get(), Velocity, FResolveRect(), SliceIndex);
	AddEnqueueCopyPass(GraphBuilder, OutReadbacks.ObstacleSdf.Get(), ObstacleSdf, FResolveRect(), SliceIndex);

	int32 LastCycleCount = 0;
	for (const int32 RecordedCycleCount : RecordedCycleCounts)
	{
		LastCycleCount = FMath::Max(LastCycleCount, RecordedCycleCount);
	}

	for (int32 CycleCount = 0; CycleCount <= LastCycleCount; ++CycleCount)
	{
		if (CycleCount > 0)
		{
			AddMultigridCyclePasses(Context, Levels, Velocity, Pressure, SmoothedPressure, CycleCount == 1, 1.0f, nullptr);
		}
		if (!RecordedCycleCounts.Contains(CycleCount))
		{
			continue;
		}

		TUniquePtr<FRHIGPUTextureReadback>& Readback = OutReadbacks.Pressures.Add_GetRef(MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.ProbePressureReadback")));
		AddEnqueueCopyPass(GraphBuilder, Readback.Get(), Pressure, FResolveRect(), SliceIndex);
		OutReadbacks.RecordedCycleCounts.Add(CycleCount);
	}
}

int64 ComputeTDFluidGpuBatchStateBytes(const FTDFluidGpuBatch& Batch)
{
	const FTDFluidGpuBatchStateTextures& State = Batch.StateTextures;
	int64 TotalBytes = 0;
	for (int32 PingPongIndex = 0; PingPongIndex < 2; ++PingPongIndex)
	{
		TotalBytes += ComputePooledTextureBytes(State.Velocity[PingPongIndex]);
		TotalBytes += ComputePooledTextureBytes(State.Density[PingPongIndex]);
		TotalBytes += ComputePooledTextureBytes(State.SurfaceDisplacement[PingPongIndex]);
		TotalBytes += ComputePooledTextureBytes(State.PressureSolution[PingPongIndex]);
		TotalBytes += ComputePooledTextureBytes(State.Heat[PingPongIndex]);
	}
	TotalBytes += ComputePooledTextureBytes(State.ObstacleSdf);
	TotalBytes += ComputePooledTextureBytes(State.TerrainElevation);
	TotalBytes += ComputePooledTextureBytes(State.PainterPrimaryDensity);
	TotalBytes += ComputePooledTextureBytes(State.PainterSelectiveDensity);
	return TotalBytes;
}

FTDFluidGpuReadbackRing::FTDFluidGpuReadbackRing()
{
	for (FTDFluidGpuReadbackSlot& Slot : Slots)
	{
		Slot.Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.ReadbackAtlas"));
	}
}

FTDFluidGpuReadbackRing::~FTDFluidGpuReadbackRing() = default;

bool FTDFluidGpuReadbackRing::Enqueue(FRDGBuilder& GraphBuilder, const FTDFluidGpuFrameOutputs& FrameOutputs)
{
	if (!FrameOutputs.ReadbackAtlas)
	{
		return false;
	}

	FTDFluidGpuReadbackSlot* FreeSlot = nullptr;
	for (FTDFluidGpuReadbackSlot& Slot : Slots)
	{
		if (!Slot.bIsPending)
		{
			FreeSlot = &Slot;
			break;
		}
	}

	if (!FreeSlot)
	{
		++DroppedCount;
		return false;
	}

	AddEnqueueCopyPass(GraphBuilder, FreeSlot->Readback.Get(), FrameOutputs.ReadbackAtlas);
	FreeSlot->Layout = FrameOutputs.ReadbackLayout;
	FreeSlot->FrameNumber = FrameOutputs.FrameNumber;
	FreeSlot->EnqueueOrder = NextEnqueueOrder++;
	FreeSlot->bIsPending = true;
	return true;
}

bool FTDFluidGpuReadbackRing::TryReadLatest(FTDFluidGpuReadbackFrame& OutFrame)
{
	FTDFluidGpuReadbackSlot* LatestReadySlot = nullptr;
	for (FTDFluidGpuReadbackSlot& Slot : Slots)
	{
		const bool bIsNewerReadySlot = Slot.bIsPending && Slot.Readback->IsReady() && (!LatestReadySlot || Slot.EnqueueOrder > LatestReadySlot->EnqueueOrder);
		if (bIsNewerReadySlot)
		{
			LatestReadySlot = &Slot;
		}
	}

	if (!LatestReadySlot)
	{
		return false;
	}

	const FIntPoint AtlasSize = LatestReadySlot->Layout.GetAtlasSize();
	int32 RowPitchInPixels = 0;
	int32 BufferHeight = 0;
	const FFloat16Color* SourceTexels = static_cast<const FFloat16Color*>(LatestReadySlot->Readback->Lock(RowPitchInPixels, &BufferHeight));
	if (!SourceTexels)
	{
		return false;
	}

	OutFrame.FrameNumber = LatestReadySlot->FrameNumber;
	OutFrame.Layout = LatestReadySlot->Layout;
	OutFrame.Texels.SetNumUninitialized(AtlasSize.X * AtlasSize.Y);
	const int32 CopiedRowCount = FMath::Min(AtlasSize.Y, BufferHeight > 0 ? BufferHeight : AtlasSize.Y);
	for (int32 RowIndex = 0; RowIndex < CopiedRowCount; ++RowIndex)
	{
		FMemory::Memcpy(OutFrame.Texels.GetData() + RowIndex * AtlasSize.X, SourceTexels + RowIndex * RowPitchInPixels, sizeof(FFloat16Color) * AtlasSize.X);
	}
	LatestReadySlot->Readback->Unlock();

	const uint64 ConsumedOrder = LatestReadySlot->EnqueueOrder;
	for (FTDFluidGpuReadbackSlot& Slot : Slots)
	{
		if (Slot.bIsPending && Slot.EnqueueOrder <= ConsumedOrder)
		{
			Slot.bIsPending = false;
		}
	}
	return true;
}

int32 FTDFluidGpuReadbackRing::GetPendingCount() const
{
	int32 PendingCount = 0;
	for (const FTDFluidGpuReadbackSlot& Slot : Slots)
	{
		PendingCount += Slot.bIsPending ? 1 : 0;
	}
	return PendingCount;
}
