#include "TDFluidGpuStepPasses.h"

#include "TDFluidGpuMultigridProbeReadbacks.h"
#include "TDFluidGpuPassTimestamps.h"
#include "TDFluidGpuMassInput.h"
#include "TDFluidGpuWaterPressureProbeReadbacks.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "PooledRenderTarget.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"
#include "TextureResource.h"

DECLARE_GPU_STAT(TDFluidGpuSolver);
DECLARE_GPU_STAT(TDFluidGpuWater);
DECLARE_GPU_STAT(TDFluidGpuWaterPressure);
CSV_DEFINE_CATEGORY(TDFluidGpu, true);

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
	constexpr EPixelFormat PainterVelocityFormat = PF_G32R32F;
	constexpr EPixelFormat TerrainShiftStageFormat = PF_R32_FLOAT;
	constexpr EPixelFormat OutputFormat = PF_FloatRGBA;
	constexpr EPixelFormat ReadbackAtlasFormat = PF_FloatRGBA;
	constexpr EPixelFormat MultigridValueFormat = PF_R32_FLOAT;
	constexpr EPixelFormat MultigridApertureFormat = PF_G16R16F;
	constexpr EPixelFormat MultigridComponentLabelFormat = PF_R32_UINT;
	constexpr EPixelFormat HeatFormat = PF_R32_FLOAT;
	constexpr int32 TransmittanceGroupSize = 8;
	constexpr EPixelFormat WaterGuideFieldFormat = PF_A32B32G32R32F;
	constexpr EPixelFormat WaterScalarFieldFormat = PF_R32_FLOAT;
	constexpr EPixelFormat WaterWetnessFormat = PF_R32_FLOAT;
	constexpr EPixelFormat WaterCompositeSdfFormat = PF_R16F;
	constexpr EPixelFormat WaterRippleHalfFormat = PF_G16R16F;
	constexpr EPixelFormat WaterRippleFloatFormat = PF_G32R32F;
	constexpr EPixelFormat WaterVelocityFloatFormat = PF_G32R32F;
	constexpr EPixelFormat WaterOpenBoundaryFormat = PF_G16R16F;
	constexpr EPixelFormat WaterUpsampledPressureFormat = PF_R32_FLOAT;
	constexpr EPixelFormat WaterFieldDrySdfFormat = PF_R16F;
	constexpr int32 WaterLinearGroupSize = 64;
	constexpr int32 WaterHalfPressureLevel = 1;
	constexpr int32 MaximumWaterHalfPostSmoothIterations = 16;
	constexpr uint32 WaterProbeReadbackBytes = static_cast<uint32>(TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame * sizeof(FTDFluidGpuWaterProbeSample));
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
	class FPainterVelocityDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_PAINTER_VELOCITY");
	using FPermutationDomain = TShaderPermutationDomain<FPhaseDimension, FPainterDimension, FPainterVelocityDimension>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FPainterVelocityDimension>() && !PermutationVector.Get<FPainterDimension>())
		{
			return false;
		}
		return FTDFluidGpuComputeShader::ShouldCompilePermutation(Parameters);
	}

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
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, PainterVelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, PainterVelocityStageOut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, PainterVelocityStageIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, PainterVelocityOut)
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

class FTDFluidGpuWrapAreaDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_WRAP_AREA");
class FTDFluidGpuMassInputDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_MASS_INPUT");

class FTDFluidGpuMassSplatPointsCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuMassSplatPointsCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuMassSplatPointsCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedMassPoint>, MassPoints)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuMassInputEntry>, MassInputEntries)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, MassInputAccumulation)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, MassInputCounters)
		SHADER_PARAMETER(uint32, MassPointCount)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuMassSplatPointsCS, "/TDFluidGpu/Private/TDFluidGpuMassSplatPoints.usf", "MainCS", SF_Compute);

class FTDFluidGpuApplyStampsCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuApplyStampsCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuApplyStampsCS, FTDFluidGpuComputeShader);
	using FPermutationDomain = TShaderPermutationDomain<FTDFluidGpuWrapAreaDimension, FTDFluidGpuMassInputDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedStamp>, Stamps)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuStampBinRange>, StampBinRanges)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, StampBinIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedRingShape>, StampRingShapes)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, SurfaceDisplacementIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, DensityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, SurfaceDisplacementOut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, HeatIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, HeatOut)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuMassInputEntry>, MassInputEntries)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, MassInputAccumulation)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, MassInputCounters)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuApplyStampsCS, "/TDFluidGpu/Private/TDFluidGpuApplyStamps.usf", "MainCS", SF_Compute);

class FTDFluidGpuForcesCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuForcesCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuForcesCS, FTDFluidGpuComputeShader);
	using FPermutationDomain = TShaderPermutationDomain<FTDFluidGpuWrapAreaDimension>;

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
	using FPermutationDomain = TShaderPermutationDomain<FTDFluidGpuWrapAreaDimension>;

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
	class FOpenWaterBordersDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_OPEN_WATER_BORDERS");
	class FTileSkipDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_JACOBI_TILE_SKIP");
	class FFusedTailDimension : SHADER_PERMUTATION_INT("TDFLUIDGPU_WATER_JACOBI_TAIL", 3);
	using FPermutationDomain = TShaderPermutationDomain<FComputesDivergenceDimension, FOpenWaterBordersDimension, FTileSkipDimension, FFusedTailDimension, FTDFluidGpuWrapAreaDimension>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		const bool bUsesOpenWaterBorders = PermutationVector.Get<FOpenWaterBordersDimension>();
		if (PermutationVector.Get<FTDFluidGpuWrapAreaDimension>() && (bUsesOpenWaterBorders || PermutationVector.Get<FTileSkipDimension>() || PermutationVector.Get<FFusedTailDimension>() != 0))
		{
			return false;
		}
		if (PermutationVector.Get<FTileSkipDimension>() && !bUsesOpenWaterBorders)
		{
			return false;
		}

		if (PermutationVector.Get<FFusedTailDimension>() != 0 && (!bUsesOpenWaterBorders || PermutationVector.Get<FComputesDivergenceDimension>() || PermutationVector.Get<FTileSkipDimension>()))
		{
			return false;
		}

		return FTDFluidGpuComputeShader::ShouldCompilePermutation(Parameters);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, DivergenceIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, DivergenceOut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PressureOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, PressurePartialSumsOut)
		SHADER_PARAMETER(uint32, IterationCount)
		SHADER_PARAMETER(uint32, EnforcesObstacleBoundaries)
		SHADER_PARAMETER(uint32, TilesPerSlice)
		SHADER_PARAMETER(uint32, TilesPerRow)
		SHADER_PARAMETER(float, TileSkipBandCells)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterSurfaceHeightField)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, WaterOpenBorderFlags)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuJacobiTileCS, "/TDFluidGpu/Private/TDFluidGpuPressure.usf", "JacobiTileCS", SF_Compute);

class FTDFluidGpuSubtractGradientCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuSubtractGradientCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuSubtractGradientCS, FTDFluidGpuComputeShader);

	class FOpenWaterBordersDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_OPEN_WATER_BORDERS");
	using FPermutationDomain = TShaderPermutationDomain<FOpenWaterBordersDimension, FTDFluidGpuWrapAreaDimension>;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FTDFluidGpuWrapAreaDimension>() && PermutationVector.Get<FOpenWaterBordersDimension>())
		{
			return false;
		}
		return FTDFluidGpuComputeShader::ShouldCompilePermutation(Parameters);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
		SHADER_PARAMETER(uint32, EnforcesObstacleBoundaries)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterSurfaceHeightField)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, WaterOpenBorderFlags)
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
	class FOpenBoundaryDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_MG_OPEN_BOUNDARY");
	class FSkipsInactiveTilesDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_MG_SKIPS_INACTIVE_TILES");
	using FPermutationDomain = TShaderPermutationDomain<FFinestLevelDimension, FWritesCoarseAperturesDimension, FOpenBoundaryDimension, FSkipsInactiveTilesDimension>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FOpenBoundaryDimension>() && PermutationVector.Get<FFinestLevelDimension>())
		{
			return false;
		}

		if (PermutationVector.Get<FSkipsInactiveTilesDimension>() && !PermutationVector.Get<FOpenBoundaryDimension>())
		{
			return false;
		}

		return FTDFluidGpuComputeShader::ShouldCompilePermutation(Parameters);
	}

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
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, OpenBoundaryIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, CoarseOpenBoundaryOut)
		SHADER_PARAMETER(float, OpenBoundaryDirichletScale)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuMultigridDownCS, "/TDFluidGpu/Private/TDFluidGpuMultigrid.usf", "MultigridDownCS", SF_Compute);

class FTDFluidGpuMultigridUpCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuMultigridUpCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuMultigridUpCS, FTDFluidGpuComputeShader);

	class FFinestLevelDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_MG_IS_FINEST_LEVEL");
	class FWritesPressurePartialSumsDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_MG_WRITES_PRESSURE_PARTIAL_SUMS");
	class FOpenBoundaryDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_MG_OPEN_BOUNDARY");
	class FSkipsInactiveTilesDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_MG_SKIPS_INACTIVE_TILES");
	using FPermutationDomain = TShaderPermutationDomain<FFinestLevelDimension, FWritesPressurePartialSumsDimension, FOpenBoundaryDimension, FSkipsInactiveTilesDimension>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FOpenBoundaryDimension>() && PermutationVector.Get<FFinestLevelDimension>())
		{
			return false;
		}

		if (PermutationVector.Get<FSkipsInactiveTilesDimension>() && !PermutationVector.Get<FOpenBoundaryDimension>())
		{
			return false;
		}

		return FTDFluidGpuComputeShader::ShouldCompilePermutation(Parameters);
	}

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
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, OpenBoundaryIn)
		SHADER_PARAMETER(float, OpenBoundaryDirichletScale)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuMultigridUpCS, "/TDFluidGpu/Private/TDFluidGpuMultigrid.usf", "MultigridUpCS", SF_Compute);

class FTDFluidGpuMultigridCoarseSolveCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuMultigridCoarseSolveCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuMultigridCoarseSolveCS, FTDFluidGpuComputeShader);

	class FOpenBoundaryDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_MG_OPEN_BOUNDARY");
	using FPermutationDomain = TShaderPermutationDomain<FOpenBoundaryDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, RhsIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, AperturesIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, CorrectionOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<uint>, ComponentLabels)
		SHADER_PARAMETER(float, LevelWeightScale)
		SHADER_PARAMETER(uint32, ComputesComponentLabels)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, OpenBoundaryIn)
		SHADER_PARAMETER(float, OpenBoundaryDirichletScale)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuMultigridCoarseSolveCS, "/TDFluidGpu/Private/TDFluidGpuMultigridCoarse.usf", "MultigridCoarseSolveCS", SF_Compute);

class FTDFluidGpuAdvectVelocityCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuAdvectVelocityCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuAdvectVelocityCS, FTDFluidGpuComputeShader);
	using FPermutationDomain = TShaderPermutationDomain<FTDFluidGpuWrapAreaDimension>;

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
	using FPermutationDomain = TShaderPermutationDomain<FWritesOutputDimension, FTDFluidGpuWrapAreaDimension>;

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
	class FWritesPainterVelocityDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_WRITES_PAINTER_VELOCITY");
	using FPermutationDomain = TShaderPermutationDomain<FWritesOutputDimension, FWritesPainterVelocityDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedStamp>, Stamps)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuStampBinRange>, StampBinRanges)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, StampBinIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedObstacle>, Obstacles)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedRingShape>, StampRingShapes)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PainterPrimaryDensity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PainterSelectiveDensity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, PainterVelocity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DensityPressureOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, VelocityOutput)
		SHADER_PARAMETER(uint32, EntryIndex)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuPainterStepCS, "/TDFluidGpu/Private/TDFluidGpuPainter.usf", "MainCS", SF_Compute);

class FTDFluidGpuPainterBankBlurRowsCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuPainterBankBlurRowsCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuPainterBankBlurRowsCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PainterPrimaryDensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, BankRowsOut)
		SHADER_PARAMETER(uint32, EntryIndex)
		SHADER_PARAMETER(uint32, WrapsArea)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuPainterBankBlurRowsCS, "/TDFluidGpu/Private/TDFluidGpuPainterBank.usf", "BlurRowsCS", SF_Compute);

class FTDFluidGpuPainterBankComposeCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuPainterBankComposeCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuPainterBankComposeCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedObstacle>, Obstacles)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PainterPrimaryDensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PainterSelectiveDensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, BankRowsIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DensityPressureOutput)
		SHADER_PARAMETER(uint32, EntryIndex)
		SHADER_PARAMETER(uint32, WrapsArea)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuPainterBankComposeCS, "/TDFluidGpu/Private/TDFluidGpuPainterBank.usf", "ComposeOutputCS", SF_Compute);

class FTDFluidGpuWaterShiftCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterShiftCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterShiftCS, FTDFluidGpuComputeShader);

	class FPhaseDimension : SHADER_PERMUTATION_INT("TDFLUIDGPU_WATER_SHIFT_PHASE", 3);
	using FPermutationDomain = TShaderPermutationDomain<FPhaseDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, EntryList)
		SHADER_PARAMETER(uint32, ShiftEntryCount)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, RippleIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WetnessIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityStageOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, DensityStageOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, RippleStageOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PressureStageOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, WetnessStageOut)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityStageIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityStageIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, RippleStageIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureStageIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WetnessStageIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, DensityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, RippleOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PressureOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, WetnessOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, ObstacleSdfOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float4>, WaterGuideFieldOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, WaterSurfaceHeightFieldOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, WaterTerrainFieldOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterShiftCS, "/TDFluidGpu/Private/TDFluidGpuWaterShift.usf", "MainCS", SF_Compute);

class FTDFluidGpuWaterGuideUpdateCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterGuideUpdateCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterGuideUpdateCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedWaterGuideUpdate>, GuideUpdates)
		SHADER_PARAMETER(uint32, GuideUpdateCount)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float4>, WaterGuideFieldOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, WaterSurfaceHeightFieldOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterGuideUpdateCS, "/TDFluidGpu/Private/TDFluidGpuWaterFields.usf", "GuideUpdateCS", SF_Compute);

class FTDFluidGpuWaterTerrainUpdateCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterTerrainUpdateCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterTerrainUpdateCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedWaterTerrainUpdate>, TerrainUpdates)
		SHADER_PARAMETER(uint32, TerrainUpdateCount)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, WaterTerrainFieldOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterTerrainUpdateCS, "/TDFluidGpu/Private/TDFluidGpuWaterFields.usf", "TerrainUpdateCS", SF_Compute);

class FTDFluidGpuWaterFieldDryCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterFieldDryCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterFieldDryCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterSurfaceHeightField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterTerrainField)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, FieldDrySdfOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterFieldDryCS, "/TDFluidGpu/Private/TDFluidGpuWaterFields.usf", "FieldDryCS", SF_Compute);

class FTDFluidGpuWaterComposeCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterComposeCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterComposeCS, FTDFluidGpuComputeShader);

	class FInterpolatesFieldDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_WATER_INTERPOLATES_FIELD");
	using FPermutationDomain = TShaderPermutationDomain<FInterpolatesFieldDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, FieldDrySdf)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, CompositeSdfOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterComposeCS, "/TDFluidGpu/Private/TDFluidGpuWaterFields.usf", "ComposeCS", SF_Compute);

class FTDFluidGpuWaterOpenBorderScanCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterOpenBorderScanCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterOpenBorderScanCS, FTDFluidGpuComputeShader);

	class FWideScanDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_WATER_WIDE_BORDER_SCAN");
	using FPermutationDomain = TShaderPermutationDomain<FWideScanDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterSurfaceHeightField)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, WaterOpenBorderFlagsOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterOpenBorderScanCS, "/TDFluidGpu/Private/TDFluidGpuWaterFields.usf", "OpenBorderScanCS", SF_Compute);

class FTDFluidGpuWaterStampsCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterStampsCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterStampsCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedStamp>, Stamps)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuStampBinRange>, StampBinRanges)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, StampBinIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, StampRippleImpulses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedRingShape>, StampRingShapes)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, WaterGuideField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, RippleIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, DensityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, RippleOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterStampsCS, "/TDFluidGpu/Private/TDFluidGpuWaterStep.usf", "StampsCS", SF_Compute);

class FTDFluidGpuWaterAuxPackCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterAuxPackCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterAuxPackCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ObstacleSdf)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, AuxOutput)
		SHADER_PARAMETER(uint32, SliceIndex)
		SHADER_PARAMETER(uint32, Resolution)
		SHADER_PARAMETER(float, InverseRangeCm)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterAuxPackCS, "/TDFluidGpu/Private/TDFluidGpuWaterAux.usf", "WaterAuxPackCS", SF_Compute);

class FTDFluidGpuWaterStampsForcesVorticityCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterStampsForcesVorticityCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterStampsForcesVorticityCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedStamp>, Stamps)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuStampBinRange>, StampBinRanges)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, StampBinIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, StampRippleImpulses)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuPackedRingShape>, StampRingShapes)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, WaterGuideField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterSurfaceHeightField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterTerrainField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, RippleIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, DensityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, RippleOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterStampsForcesVorticityCS, "/TDFluidGpu/Private/TDFluidGpuWaterStep.usf", "StampsForcesVorticityCS", SF_Compute);

class FTDFluidGpuWaterForcesCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterForcesCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterForcesCS, FTDFluidGpuComputeShader);

	class FSolidFirstReadsDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_WATER_SOLID_FIRST_READS");
	using FPermutationDomain = TShaderPermutationDomain<FSolidFirstReadsDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, WaterGuideField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterSurfaceHeightField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterTerrainField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterForcesCS, "/TDFluidGpu/Private/TDFluidGpuWaterStep.usf", "ForcesCS", SF_Compute);

class FTDFluidGpuWaterAdvectVelocityCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterAdvectVelocityCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterAdvectVelocityCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, WaterGuideField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, VelocityOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterAdvectVelocityCS, "/TDFluidGpu/Private/TDFluidGpuWaterStep.usf", "AdvectVelocityCS", SF_Compute);

class FTDFluidGpuWaterRippleSubstepCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterRippleSubstepCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterRippleSubstepCS, FTDFluidGpuComputeShader);

	class FSolidFirstReadsDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_WATER_SOLID_FIRST_READS");
	using FPermutationDomain = TShaderPermutationDomain<FSolidFirstReadsDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, RippleIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, RippleOut)
		SHADER_PARAMETER(uint32, SubstepIndex)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterRippleSubstepCS, "/TDFluidGpu/Private/TDFluidGpuWaterRipple.usf", "RippleSubstepCS", SF_Compute);

class FTDFluidGpuWaterFinalizeCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterFinalizeCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterFinalizeCS, FTDFluidGpuComputeShader);

	class FWritesOutputDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_WATER_WRITES_OUTPUT");
	class FHalfPressureDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_WATER_HALF_PRESSURE");
	class FSolidFirstReadsDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_WATER_SOLID_FIRST_READS");
	using FPermutationDomain = TShaderPermutationDomain<FWritesOutputDimension, FHalfPressureDimension, FSolidFirstReadsDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, WaterGuideField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterSurfaceHeightField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterTerrainField)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, WaterOpenBorderFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, MeanPressure)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, RippleIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, DensityOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, RippleOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PressureSolution)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, WetnessState)
		SHADER_PARAMETER(uint32, EntryIndex)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureFull)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DensityPressureOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, VelocityOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, WaterSurfaceOutput)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterFinalizeCS, "/TDFluidGpu/Private/TDFluidGpuWaterFinalize.usf", "FinalizeCS", SF_Compute);

class FTDFluidGpuWaterProbeCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterProbeCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterProbeCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterProbeRequest>, ProbeRequests)
		SHADER_PARAMETER(uint32, ProbeRequestCount)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, WaterGuideField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterSurfaceHeightField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterTerrainField)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, DensityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, RippleIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WetnessIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FTDFluidGpuWaterProbeSample>, ProbeSamplesOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterProbeCS, "/TDFluidGpu/Private/TDFluidGpuWaterFinalize.usf", "ProbeCS", SF_Compute);

class FTDFluidGpuWaterHalfPressurePrepareCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterHalfPressurePrepareCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterHalfPressurePrepareCS, FTDFluidGpuComputeShader);

	class FSharedAperturesDimension : SHADER_PERMUTATION_BOOL("TDFLUIDGPU_WATER_SHARED_APERTURES");
	using FPermutationDomain = TShaderPermutationDomain<FSharedAperturesDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterSurfaceHeightField)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, WaterOpenBorderFlags)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, VelocityIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureIn)
		SHADER_PARAMETER(float, ClosedWarmStartScale)
		SHADER_PARAMETER(float, OpenBoundaryDirichletScale)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, RhsOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, AperturesOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float2>, OpenBoundaryOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterHalfPressurePrepareCS, "/TDFluidGpu/Private/TDFluidGpuWaterPressure.usf", "HalfPrepareCS", SF_Compute);

class FTDFluidGpuWaterHalfPressureApplyCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterHalfPressureApplyCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterHalfPressureApplyCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CorrectionIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, WaterOpenBorderFlags)
		SHADER_PARAMETER(float, ClosedWarmStartScale)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PressureSolution)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterHalfPressureApplyCS, "/TDFluidGpu/Private/TDFluidGpuWaterPressure.usf", "HalfApplyCS", SF_Compute);

class FTDFluidGpuWaterUpsamplePressureCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterUpsamplePressureCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterUpsamplePressureCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float2>, AperturesIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, WaterSurfaceHeightField)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, WaterOpenBorderFlags)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, PressureFullOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterUpsamplePressureCS, "/TDFluidGpu/Private/TDFluidGpuWaterPressure.usf", "UpsampleCS", SF_Compute);

class FTDFluidGpuWaterHalfRhsMeanTilesCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterHalfRhsMeanTilesCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterHalfRhsMeanTilesCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, HalfRhsIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, HalfRhsPartialSumsOut)
		SHADER_PARAMETER(uint32, TilesPerSlice)
		SHADER_PARAMETER(uint32, TilesPerRow)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterHalfRhsMeanTilesCS, "/TDFluidGpu/Private/TDFluidGpuWaterPressure.usf", "HalfRhsMeanTilesCS", SF_Compute);

class FTDFluidGpuWaterPressureMeanTilesCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterPressureMeanTilesCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterPressureMeanTilesCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, PressureIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, WaterOpenBorderFlags)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, WaterPressurePartialSumsOut)
		SHADER_PARAMETER(uint32, TilesPerSlice)
		SHADER_PARAMETER(uint32, TilesPerRow)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterPressureMeanTilesCS, "/TDFluidGpu/Private/TDFluidGpuWaterPressure.usf", "WaterPressureMeanTilesCS", SF_Compute);

class FTDFluidGpuWaterHalfRhsSubtractMeanCS : public FTDFluidGpuComputeShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuWaterHalfRhsSubtractMeanCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuWaterHalfRhsSubtractMeanCS, FTDFluidGpuComputeShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuActorParameters>, ActorParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuWaterParameters>, WaterParameters)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, CompositeSdf)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, WaterOpenBorderFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, HalfRhsMeanIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, HalfRhsInOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuWaterHalfRhsSubtractMeanCS, "/TDFluidGpu/Private/TDFluidGpuWaterPressure.usf", "HalfRhsSubtractMeanCS", SF_Compute);

namespace
{
	struct FTDFluidGpuFrameResources
	{
		FRDGBufferSRVRef Stamps = nullptr;
		FRDGBufferSRVRef StampBinRanges = nullptr;
		FRDGBufferSRVRef StampBinIndices = nullptr;
		FRDGTextureRef ReadbackAtlas = nullptr;
		FRDGTextureUAVRef ReadbackAtlasUAV = nullptr;
		FRDGBufferSRVRef StampRippleImpulses = nullptr;
		FRDGBufferSRVRef StampRingShapes = nullptr;
		FRDGBufferRef WaterProbeSamples = nullptr;
	};

	struct FTDFluidGpuMassInputSnapshot
	{
		FRDGBufferRef Buffer = nullptr;
		int32 Capacity = 0;
		TArray<uint64> ActorKeys;
	};

	struct FTDFluidGpuMassInputGraph
	{
		FTDFluidGpuMassInputSnapshot* Snapshot = nullptr;
		TArray<uint32> CounterBases;
		TArray<uint64> ActorKeys;
		FRDGBufferRef Accumulation = nullptr;
		FRDGBufferRef Counters = nullptr;
		FRDGBufferSRVRef Entries = nullptr;
		TArray<FTDFluidGpuPackedMassPoint> Points;
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
		FRDGTextureRef PainterVelocity = nullptr;
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
		FTDFluidGpuMassInputGraph* MassInput = nullptr;
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

	struct FTDFluidGpuWaterGraphTextures
	{
		FRDGTextureRef Ripple[2] = { nullptr, nullptr };
		FRDGTextureRef GuideField = nullptr;
		FRDGTextureRef SurfaceHeightField = nullptr;
		FRDGTextureRef TerrainField = nullptr;
		FRDGTextureRef Wetness = nullptr;
		FRDGTextureRef FieldDrySdf = nullptr;
		FRDGTextureRef CompositeSdf = nullptr;
		FRDGBufferRef OpenBorderFlags = nullptr;
		FRDGBufferSRVRef WaterParameters = nullptr;
	};

	struct FTDFluidGpuWaterPressureBindings
	{
		FRDGBufferSRVRef WaterParameters = nullptr;
		FRDGTextureRef SurfaceHeightField = nullptr;
		FRDGBufferRef OpenBorderFlags = nullptr;
	};

	struct FTDFluidGpuWaterOutputTargets
	{
		FRDGTextureRef DensityPressure = nullptr;
		FRDGTextureRef Velocity = nullptr;
		FRDGTextureRef WaterSurface = nullptr;
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
			if (Batch.GetKey().bWritesPainterVelocity)
			{
				Textures.PainterVelocity = RegisterOrGrowStateTexture(GraphBuilder, State.PainterVelocity, Resolution, SliceCapacity, PainterVelocityFormat, true, TEXT("TDFluidGpu.PainterVelocity"));
			}
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

	void CreateWaterFrameResources(FRDGBuilder& GraphBuilder, const FTDFluidGpuPreparedFrame& PreparedFrame, FTDFluidGpuFrameResources& InOutResources)
	{
		if (!PreparedFrame.bHasWaterEntries)
		{
			return;
		}

		InOutResources.StampRippleImpulses = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.StampRippleImpulses"), PreparedFrame.StampBins.RippleImpulsesCm));
		if (PreparedFrame.WaterProbeSlotCount <= 0)
		{
			return;
		}

		TArray<FTDFluidGpuWaterProbeSample> DefaultProbeSamples;
		DefaultProbeSamples.Init(FTDFluidGpuWaterProbeSample(), TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame);
		InOutResources.WaterProbeSamples = CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.WaterProbeSamples"), MoveTemp(DefaultProbeSamples));
	}

	FTDFluidGpuFrameResources CreateFrameResources(FRDGBuilder& GraphBuilder, const FTDFluidGpuPreparedFrame& PreparedFrame)
	{
		FTDFluidGpuFrameResources Resources;
		Resources.Stamps = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Stamps"), PreparedFrame.StampBins.Stamps));
		Resources.StampBinRanges = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.StampBinRanges"), PreparedFrame.StampBins.BinRanges));
		Resources.StampBinIndices = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.StampBinIndices"), PreparedFrame.StampBins.BinStampIndices));
		Resources.StampRingShapes = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.StampRingShapes"), PreparedFrame.StampBins.RingShapes));
		CreateWaterFrameResources(GraphBuilder, PreparedFrame, Resources);

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

	void AddShiftGatherPass(const FTDFluidGpuBatchPassContext& Context, FRDGTextureRef CompactStage, FRDGTextureRef PainterVelocityStage)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchFrame& Frame = Context.Batch.GetFrame();
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		const bool bIsPainter = IsPainterBatch(Context.Batch);

		FTDFluidGpuShiftCS::FPermutationDomain Permutation;
		Permutation.Set<FTDFluidGpuShiftCS::FPhaseDimension>(ShiftPhaseGather);
		Permutation.Set<FTDFluidGpuShiftCS::FPainterDimension>(bIsPainter);
		Permutation.Set<FTDFluidGpuShiftCS::FPainterVelocityDimension>(PainterVelocityStage != nullptr);

		FTDFluidGpuShiftCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuShiftCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->EntryList = UploadEntryList(GraphBuilder, TEXT("TDFluidGpu.ShiftEntries"), TArray<uint32>(Frame.ShiftEntryIndices));
		Parameters->ShiftEntryCount = static_cast<uint32>(Frame.ShiftEntryIndices.Num());
		if (bIsPainter)
		{
			Parameters->PainterPrimaryDensityIn = MakeSRV(GraphBuilder, Textures.PainterPrimaryDensity);
			Parameters->PainterSelectiveDensityIn = MakeSRV(GraphBuilder, Textures.PainterSelectiveDensity);
			Parameters->PainterStageOut = MakeUAV(GraphBuilder, CompactStage);
			if (PainterVelocityStage)
			{
				Parameters->PainterVelocityIn = MakeSRV(GraphBuilder, Textures.PainterVelocity);
				Parameters->PainterVelocityStageOut = MakeUAV(GraphBuilder, PainterVelocityStage);
			}
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
		const bool bShiftsPainterVelocity = bIsPainter && Textures.PainterVelocity != nullptr;
		FRDGTextureRef CompactStage = nullptr;
		FRDGTextureRef PainterVelocityStage = nullptr;
		if (ShiftCount > 0)
		{
			CompactStage = GraphBuilder.CreateTexture(MakeStateArrayDesc(Context.Resolution, ShiftCount, bIsPainter ? PainterShiftStageFormat : TerrainShiftStageFormat), TEXT("TDFluidGpu.ShiftStage"));
			if (bShiftsPainterVelocity)
			{
				PainterVelocityStage = GraphBuilder.CreateTexture(MakeStateArrayDesc(Context.Resolution, ShiftCount, PainterVelocityFormat), TEXT("TDFluidGpu.PainterVelocityShiftStage"));
			}
			AddShiftGatherPass(Context, CompactStage, PainterVelocityStage);
		}

		TArray<uint32> ScatterEntries(Frame.ShiftEntryIndices);
		ScatterEntries.Append(Frame.ResetEntryIndices);

		FTDFluidGpuShiftCS::FPermutationDomain Permutation;
		Permutation.Set<FTDFluidGpuShiftCS::FPhaseDimension>(CompactStage ? ShiftPhaseScatter : ShiftPhaseResetOnly);
		Permutation.Set<FTDFluidGpuShiftCS::FPainterDimension>(bIsPainter);
		Permutation.Set<FTDFluidGpuShiftCS::FPainterVelocityDimension>(bShiftsPainterVelocity);

		FTDFluidGpuShiftCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuShiftCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->EntryList = UploadEntryList(GraphBuilder, TEXT("TDFluidGpu.ShiftScatterEntries"), MoveTemp(ScatterEntries));
		Parameters->ShiftEntryCount = static_cast<uint32>(ShiftCount);
		if (bIsPainter)
		{
			Parameters->PainterPrimaryDensityOut = MakeUAV(GraphBuilder, Textures.PainterPrimaryDensity);
			Parameters->PainterSelectiveDensityOut = MakeUAV(GraphBuilder, Textures.PainterSelectiveDensity);
			if (bShiftsPainterVelocity)
			{
				Parameters->PainterVelocityOut = MakeUAV(GraphBuilder, Textures.PainterVelocity);
			}
			if (PainterVelocityStage)
			{
				Parameters->PainterVelocityStageIn = MakeSRV(GraphBuilder, PainterVelocityStage);
			}
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
		Parameters->StampRingShapes = Context.FrameResources.StampRingShapes;
		Parameters->ObstacleSdf = MakeSRV(GraphBuilder, Textures.ObstacleSdf);
		Parameters->VelocityIn = MakeSRV(GraphBuilder, Textures.Velocity[0]);
		Parameters->DensityIn = MakeSRV(GraphBuilder, Textures.Density[0]);
		Parameters->SurfaceDisplacementIn = MakeSRV(GraphBuilder, Textures.SurfaceDisplacement[0]);
		Parameters->VelocityOut = MakeUAV(GraphBuilder, Textures.Velocity[1]);
		Parameters->DensityOut = MakeUAV(GraphBuilder, Textures.Density[1]);
		Parameters->SurfaceDisplacementOut = MakeUAV(GraphBuilder, Textures.SurfaceDisplacement[1]);
		Parameters->HeatIn = MakeSRV(GraphBuilder, Textures.Heat[0]);
		Parameters->HeatOut = MakeUAV(GraphBuilder, Textures.Heat[1]);

		FTDFluidGpuApplyStampsCS::FPermutationDomain StampDomain;
		StampDomain.Set<FTDFluidGpuWrapAreaDimension>(Context.Batch.GetKey().bWrapsArea);
		StampDomain.Set<FTDFluidGpuMassInputDimension>(Context.MassInput != nullptr);
		if (Context.MassInput)
		{
			Parameters->MassInputEntries = Context.MassInput->Entries;
			Parameters->MassInputAccumulation = GraphBuilder.CreateSRV(Context.MassInput->Accumulation, PF_R32_UINT);
			Parameters->MassInputCounters = GraphBuilder.CreateUAV(Context.MassInput->Counters, PF_R32_UINT);
		}
		TShaderMapRef<FTDFluidGpuApplyStampsCS> Shader(Context.ShaderMap, StampDomain);
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

		FTDFluidGpuForcesCS::FPermutationDomain ForcesDomain;
		ForcesDomain.Set<FTDFluidGpuWrapAreaDimension>(Context.Batch.GetKey().bWrapsArea);
		TShaderMapRef<FTDFluidGpuForcesCS> Shader(Context.ShaderMap, ForcesDomain);
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

		FTDFluidGpuVorticityCS::FPermutationDomain VorticityDomain;
		VorticityDomain.Set<FTDFluidGpuWrapAreaDimension>(Context.Batch.GetKey().bWrapsArea);
		TShaderMapRef<FTDFluidGpuVorticityCS> Shader(Context.ShaderMap, VorticityDomain);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Vorticity"), Shader, Parameters, MakeTileGroupCount(Context.Resolution, Context.EntryCount));
	}

	template <typename TWaterBoundParameters>
	void BindWaterPressureParameters(FRDGBuilder& GraphBuilder, const FTDFluidGpuWaterPressureBindings* WaterBindings, TWaterBoundParameters& InOutParameters)
	{
		if (!WaterBindings)
		{
			return;
		}

		InOutParameters.WaterParameters = WaterBindings->WaterParameters;
		InOutParameters.WaterSurfaceHeightField = MakeSRV(GraphBuilder, WaterBindings->SurfaceHeightField);
		InOutParameters.WaterOpenBorderFlags = GraphBuilder.CreateSRV(WaterBindings->OpenBorderFlags);
	}

	struct FTDFluidGpuJacobiTileSkipOptions
	{
		bool bSkipsTiles = false;
		float BandCells = 0.0f;
	};

	struct FTDFluidGpuJacobiFusedTail
	{
		FRDGTextureRef VelocityOut = nullptr;
		FRDGBufferRef PressurePartialSums = nullptr;
		bool bEnforcesObstacleBoundaries = false;
	};

	int32 AddJacobiPassesOnTextures(const FTDFluidGpuBatchPassContext& Context, FRDGTextureRef ProjectedVelocity, FRDGTextureRef (&PressureTextures)[2], int32 CurrentPressureIndex, int32 IterationCount, const FTDFluidGpuWaterPressureBindings* WaterBindings, const FTDFluidGpuJacobiTileSkipOptions& TileSkip = FTDFluidGpuJacobiTileSkipOptions(), const FTDFluidGpuJacobiFusedTail* FusedTail = nullptr)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		const int32 PressureIterations = IterationCount;
		const bool bFusesTail = FusedTail != nullptr && WaterBindings != nullptr && PressureIterations > JacobiIterationsPerDispatch;
		const int32 MiddleDispatchCount = bFusesTail ? FMath::Max(0, FMath::DivideAndRoundUp(PressureIterations - 3 * JacobiIterationsPerDispatch, JacobiIterationsPerDispatch)) : 0;
		const int32 DispatchCount = bFusesTail ? 2 + MiddleDispatchCount : FMath::DivideAndRoundUp(PressureIterations, JacobiIterationsPerDispatch);
		const int32 TailIterationCount = PressureIterations - (1 + MiddleDispatchCount) * JacobiIterationsPerDispatch;
		const bool bSkipsTiles = TileSkip.bSkipsTiles && WaterBindings != nullptr && (TileSkip.BandCells <= 0.0f || DispatchCount == 1);
		const int32 PartialSumsTilesPerRow = FMath::DivideAndRoundUp(Context.Resolution, TileGroupSize);
		FRDGTextureRef Divergence = GraphBuilder.CreateTexture(MakeStateArrayDesc(Context.Resolution, Context.Batch.StateTextures.SliceCapacity, DivergenceFormat), TEXT("TDFluidGpu.Divergence"));

		for (int32 DispatchIndex = 0; DispatchIndex < DispatchCount; ++DispatchIndex)
		{
			const bool bComputesDivergence = DispatchIndex == 0;
			const bool bIsFusedTailDispatch = bFusesTail && DispatchIndex == DispatchCount - 1;
			const int32 TargetPressureIndex = 1 - CurrentPressureIndex;
			FTDFluidGpuJacobiTileCS::FPermutationDomain Domain;
			Domain.Set<FTDFluidGpuJacobiTileCS::FComputesDivergenceDimension>(bComputesDivergence);
			Domain.Set<FTDFluidGpuJacobiTileCS::FOpenWaterBordersDimension>(WaterBindings != nullptr);
			Domain.Set<FTDFluidGpuJacobiTileCS::FTileSkipDimension>(bSkipsTiles && !bIsFusedTailDispatch);
			Domain.Set<FTDFluidGpuJacobiTileCS::FFusedTailDimension>(bIsFusedTailDispatch ? (FusedTail->PressurePartialSums != nullptr ? 2 : 1) : 0);
			Domain.Set<FTDFluidGpuWrapAreaDimension>(Context.Batch.GetKey().bWrapsArea);
			TShaderMapRef<FTDFluidGpuJacobiTileCS> Shader(Context.ShaderMap, Domain);
			FTDFluidGpuJacobiTileCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuJacobiTileCS::FParameters>();
			Parameters->ActorParameters = Context.ActorParameters;
			Parameters->ObstacleSdf = MakeSRV(GraphBuilder, Textures.ObstacleSdf);
			Parameters->PressureIn = MakeSRV(GraphBuilder, PressureTextures[CurrentPressureIndex]);
			Parameters->PressureOut = MakeUAV(GraphBuilder, PressureTextures[TargetPressureIndex]);
			Parameters->IterationCount = static_cast<uint32>(bIsFusedTailDispatch ? TailIterationCount : FMath::Min(JacobiIterationsPerDispatch, PressureIterations - DispatchIndex * JacobiIterationsPerDispatch));
			Parameters->TileSkipBandCells = TileSkip.BandCells;
			if (bComputesDivergence)
			{
				Parameters->VelocityIn = MakeSRV(GraphBuilder, ProjectedVelocity);
				Parameters->DivergenceOut = MakeUAV(GraphBuilder, Divergence);
			}
			else
			{
				Parameters->DivergenceIn = MakeSRV(GraphBuilder, Divergence);
			}
			if (bIsFusedTailDispatch)
			{
				Parameters->VelocityIn = MakeSRV(GraphBuilder, ProjectedVelocity);
				Parameters->VelocityOut = MakeUAV(GraphBuilder, FusedTail->VelocityOut);
				Parameters->EnforcesObstacleBoundaries = FusedTail->bEnforcesObstacleBoundaries ? 1u : 0u;
				if (FusedTail->PressurePartialSums != nullptr)
				{
					Parameters->PressurePartialSumsOut = GraphBuilder.CreateUAV(FusedTail->PressurePartialSums);
					Parameters->TilesPerSlice = static_cast<uint32>(PartialSumsTilesPerRow * PartialSumsTilesPerRow);
					Parameters->TilesPerRow = static_cast<uint32>(PartialSumsTilesPerRow);
				}
			}
			BindWaterPressureParameters(GraphBuilder, WaterBindings, *Parameters);

			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.JacobiTile"), Shader, Parameters, MakeTileGroupCount(Context.Resolution, Context.EntryCount));
			CurrentPressureIndex = TargetPressureIndex;
		}
		return CurrentPressureIndex;
	}

	int32 AddJacobiPasses(const FTDFluidGpuBatchPassContext& Context, FRDGTextureRef ProjectedVelocity, int32 CurrentPressureIndex, const FTDFluidGpuWaterPressureBindings* WaterBindings = nullptr, const FTDFluidGpuJacobiTileSkipOptions& TileSkip = FTDFluidGpuJacobiTileSkipOptions(), const FTDFluidGpuJacobiFusedTail* FusedTail = nullptr)
	{
		FRDGTextureRef PressureTextures[2] = { Context.Textures.PressureSolution[0], Context.Textures.PressureSolution[1] };
		return AddJacobiPassesOnTextures(Context, ProjectedVelocity, PressureTextures, CurrentPressureIndex, FMath::Max(Context.Batch.GetKey().PressureIterations, 1), WaterBindings, TileSkip, FusedTail);
	}

	void AddSubtractGradientPass(const FTDFluidGpuBatchPassContext& Context, FRDGTextureRef Pressure, FRDGTextureRef VelocityIn, FRDGTextureRef VelocityOut, bool bEnforcesObstacleBoundaries, const FTDFluidGpuWaterPressureBindings* WaterBindings = nullptr)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FTDFluidGpuSubtractGradientCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuSubtractGradientCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->ObstacleSdf = MakeSRV(GraphBuilder, Context.Textures.ObstacleSdf);
		Parameters->PressureIn = MakeSRV(GraphBuilder, Pressure);
		Parameters->VelocityIn = MakeSRV(GraphBuilder, VelocityIn);
		Parameters->VelocityOut = MakeUAV(GraphBuilder, VelocityOut);
		Parameters->EnforcesObstacleBoundaries = bEnforcesObstacleBoundaries ? 1u : 0u;
		BindWaterPressureParameters(GraphBuilder, WaterBindings, *Parameters);

		FTDFluidGpuSubtractGradientCS::FPermutationDomain Domain;
		Domain.Set<FTDFluidGpuSubtractGradientCS::FOpenWaterBordersDimension>(WaterBindings != nullptr);
		Domain.Set<FTDFluidGpuWrapAreaDimension>(Context.Batch.GetKey().bWrapsArea);
		TShaderMapRef<FTDFluidGpuSubtractGradientCS> Shader(Context.ShaderMap, Domain);
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
		FRDGTextureRef OpenBoundary = nullptr;
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
		bool bUsesOpenBoundary = false;
		bool bSkipsInactiveTiles = false;
	};

	bool IsMultigridBatch(const FTDFluidGpuBatch& Batch)
	{
		return Batch.GetKey().PressureSolver == ETDFluidGpuPressureSolver::Multigrid;
	}

	float ComputeMultigridLevelWeightScale(int32 Level)
	{
		return 1.0f / static_cast<float>(1 << (2 * Level));
	}

	float ComputeWaterDirichletScale(int32 Level)
	{
		const float LevelCellWidth = static_cast<float>(1 << Level);
		return LevelCellWidth / (0.5f * LevelCellWidth + TDFluidGpuSolverConstants::WaterHalfPressureDirichletBaseScale);
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
			if (Context.bUsesOpenBoundary)
			{
				LevelTextures.OpenBoundary = GraphBuilder.CreateTexture(MakeStateArrayDesc(LevelResolution, Context.EntryCount, WaterOpenBoundaryFormat), TEXT("TDFluidGpu.WaterMultigridOpenBoundary"));
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
		if (Context.bUsesOpenBoundary)
		{
			Parameters->OpenBoundaryIn = MakeSRV(GraphBuilder, Levels[Level].OpenBoundary);
			Parameters->OpenBoundaryDirichletScale = ComputeWaterDirichletScale(Level);
			if (bWritesCoarseApertures)
			{
				Parameters->CoarseOpenBoundaryOut = MakeUAV(GraphBuilder, CoarseLevel.OpenBoundary);
			}
		}

		FTDFluidGpuMultigridDownCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTDFluidGpuMultigridDownCS::FFinestLevelDimension>(bIsFinestLevel);
		PermutationVector.Set<FTDFluidGpuMultigridDownCS::FWritesCoarseAperturesDimension>(bWritesCoarseApertures);
		PermutationVector.Set<FTDFluidGpuMultigridDownCS::FOpenBoundaryDimension>(Context.bUsesOpenBoundary);
		PermutationVector.Set<FTDFluidGpuMultigridDownCS::FSkipsInactiveTilesDimension>(Context.bUsesOpenBoundary && Context.bSkipsInactiveTiles);
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
		if (Context.bUsesOpenBoundary)
		{
			Parameters->OpenBoundaryIn = MakeSRV(GraphBuilder, Coarsest.OpenBoundary);
			Parameters->OpenBoundaryDirichletScale = ComputeWaterDirichletScale(CoarsestLevel);
		}

		FTDFluidGpuMultigridCoarseSolveCS::FPermutationDomain Domain;
		Domain.Set<FTDFluidGpuMultigridCoarseSolveCS::FOpenBoundaryDimension>(Context.bUsesOpenBoundary);
		TShaderMapRef<FTDFluidGpuMultigridCoarseSolveCS> Shader(Context.ShaderMap, Domain);
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
		if (Context.bUsesOpenBoundary)
		{
			Parameters->OpenBoundaryIn = MakeSRV(GraphBuilder, Levels[Level].OpenBoundary);
			Parameters->OpenBoundaryDirichletScale = ComputeWaterDirichletScale(Level);
		}

		FTDFluidGpuMultigridUpCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTDFluidGpuMultigridUpCS::FFinestLevelDimension>(bIsFinestLevel);
		PermutationVector.Set<FTDFluidGpuMultigridUpCS::FWritesPressurePartialSumsDimension>(bWritesPressurePartialSums);
		PermutationVector.Set<FTDFluidGpuMultigridUpCS::FOpenBoundaryDimension>(Context.bUsesOpenBoundary);
		PermutationVector.Set<FTDFluidGpuMultigridUpCS::FSkipsInactiveTilesDimension>(Context.bUsesOpenBoundary && Context.bSkipsInactiveTiles);
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

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuSkipInactiveMultigridTiles(
		TEXT("TD.FluidFX.WaterGpuSkipInactiveMultigridTiles"),
		0,
		TEXT("GPU water multigrid down and up passes skip the cell setup and smoothing work of tiles whose loaded region has no open face and no open boundary weight, and write the same values the full work writes. Pressure stays bit identical, but the extra barrier measured 0.002 ms slower at 1024. 1 turns the skip on, 0 keeps the full tile work."),
		ECVF_RenderThreadSafe);

	FTDFluidGpuMultigridPassContext MakeWaterMultigridPassContext(const FTDFluidGpuBatchPassContext& Context)
	{
		FTDFluidGpuMultigridPassContext MultigridContext = MakeMultigridPassContext(Context);
		MultigridContext.bUsesOpenBoundary = true;
		MultigridContext.bSkipsInactiveTiles = CVarTDFluidWaterGpuSkipInactiveMultigridTiles.GetValueOnAnyThread() != 0;
		return MultigridContext;
	}

	void AddWaterMultigridCyclePasses(const FTDFluidGpuMultigridPassContext& Context, const FTDFluidGpuMultigridLevels& Levels, bool bWritesCoarseApertures)
	{
		const int32 CoarsestLevel = Levels.Num() - 1;
		for (int32 Level = WaterHalfPressureLevel; Level < CoarsestLevel; ++Level)
		{
			AddMultigridDownPass(Context, Levels, Level, nullptr, nullptr, nullptr, bWritesCoarseApertures, 1.0f);
			AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("mg_down_L"), Level);
		}

		AddMultigridCoarseSolvePass(Context, Levels, bWritesCoarseApertures);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("mg_coarse"));
		for (int32 Level = CoarsestLevel - 1; Level >= WaterHalfPressureLevel; --Level)
		{
			AddMultigridUpPass(Context, Levels, Level, nullptr, nullptr, nullptr, nullptr);
			AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("mg_up_L"), Level);
		}
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

		FTDFluidGpuAdvectVelocityCS::FPermutationDomain AdvectDomain;
		AdvectDomain.Set<FTDFluidGpuWrapAreaDimension>(Context.Batch.GetKey().bWrapsArea);
		TShaderMapRef<FTDFluidGpuAdvectVelocityCS> Shader(Context.ShaderMap, AdvectDomain);
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

			FTDFluidGpuAdvectDensityPackCS::FPermutationDomain DensityPackDomain;
			DensityPackDomain.Set<FTDFluidGpuAdvectDensityPackCS::FWritesOutputDimension>(bWritesOutput);
			DensityPackDomain.Set<FTDFluidGpuWrapAreaDimension>(Context.Batch.GetKey().bWrapsArea);
			TShaderMapRef<FTDFluidGpuAdvectDensityPackCS> Shader(Context.ShaderMap, DensityPackDomain);
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

	void AddPainterTrackBankPasses(const FTDFluidGpuBatchPassContext& Context, int32 EntryIndex, FRDGTextureRef DensityPressureOutput)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const uint32 WrapsArea = Context.Batch.GetKey().bWrapsArea ? 1u : 0u;
		const FRDGTextureDesc BankRowsDesc = FRDGTextureDesc::Create2D(FIntPoint(Context.Resolution, Context.Resolution), PF_R16F, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef BankRows = GraphBuilder.CreateTexture(BankRowsDesc, TEXT("TDFluidGpu.PainterBankRows"));

		FTDFluidGpuPainterBankBlurRowsCS::FParameters* RowParameters = GraphBuilder.AllocParameters<FTDFluidGpuPainterBankBlurRowsCS::FParameters>();
		RowParameters->ActorParameters = Context.ActorParameters;
		RowParameters->PainterPrimaryDensityIn = MakeSRV(GraphBuilder, Context.Textures.PainterPrimaryDensity);
		RowParameters->BankRowsOut = GraphBuilder.CreateUAV(BankRows);
		RowParameters->EntryIndex = static_cast<uint32>(EntryIndex);
		RowParameters->WrapsArea = WrapsArea;
		TShaderMapRef<FTDFluidGpuPainterBankBlurRowsCS> RowShader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.PainterBankRows"), RowShader, RowParameters, MakeCellGroupCount(Context.Resolution, 1));

		FTDFluidGpuPainterBankComposeCS::FParameters* ComposeParameters = GraphBuilder.AllocParameters<FTDFluidGpuPainterBankComposeCS::FParameters>();
		ComposeParameters->ActorParameters = Context.ActorParameters;
		ComposeParameters->Obstacles = Context.Obstacles;
		ComposeParameters->PainterPrimaryDensityIn = MakeSRV(GraphBuilder, Context.Textures.PainterPrimaryDensity);
		ComposeParameters->PainterSelectiveDensityIn = MakeSRV(GraphBuilder, Context.Textures.PainterSelectiveDensity);
		ComposeParameters->BankRowsIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(BankRows));
		ComposeParameters->DensityPressureOutput = MakeUAV(GraphBuilder, DensityPressureOutput);
		ComposeParameters->EntryIndex = static_cast<uint32>(EntryIndex);
		ComposeParameters->WrapsArea = WrapsArea;
		TShaderMapRef<FTDFluidGpuPainterBankComposeCS> ComposeShader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.PainterBankCompose"), ComposeShader, ComposeParameters, MakeCellGroupCount(Context.Resolution, 1));
	}

	void AddPainterStepPasses(const FTDFluidGpuBatchPassContext& Context, FTDFluidGpuFrameOutputs& InOutOutputs)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchFrame& Frame = Context.Batch.GetFrame();
		FRDGTextureUAVRef PrimaryUAV = MakeUnorderedUAV(GraphBuilder, Context.Textures.PainterPrimaryDensity);
		FRDGTextureUAVRef SelectiveUAV = MakeUnorderedUAV(GraphBuilder, Context.Textures.PainterSelectiveDensity);
		const bool bWritesPainterVelocity = Context.Textures.PainterVelocity != nullptr;
		FRDGTextureUAVRef PainterVelocityUAV = bWritesPainterVelocity ? MakeUnorderedUAV(GraphBuilder, Context.Textures.PainterVelocity) : nullptr;

		for (int32 EntryIndex = 0; EntryIndex < Context.EntryCount; ++EntryIndex)
		{
			const FTDFluidGpuActorInput& Actor = Context.FrameInputs.Actors[Frame.Entries[EntryIndex].ActorInputIndex];
			const FTDFluidGpuOutputTargets Targets = RegisterOutputTargets(GraphBuilder, Actor, Context.Resolution, bWritesPainterVelocity);
			const bool bHasOutputTargets = Targets.DensityPressure != nullptr;
			const bool bComposesTrackBank = bHasOutputTargets && Actor.Settings.TrackBankStrength > 0.0f;
			const bool bWritesOutput = bHasOutputTargets && (!bComposesTrackBank || bWritesPainterVelocity);

			FTDFluidGpuPainterStepCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuPainterStepCS::FParameters>();
			Parameters->ActorParameters = Context.ActorParameters;
			Parameters->Stamps = Context.FrameResources.Stamps;
			Parameters->StampBinRanges = Context.FrameResources.StampBinRanges;
			Parameters->StampBinIndices = Context.FrameResources.StampBinIndices;
			Parameters->Obstacles = Context.Obstacles;
			Parameters->StampRingShapes = Context.FrameResources.StampRingShapes;
			Parameters->PainterPrimaryDensity = PrimaryUAV;
			Parameters->PainterSelectiveDensity = SelectiveUAV;
			Parameters->PainterVelocity = PainterVelocityUAV;
			Parameters->EntryIndex = static_cast<uint32>(EntryIndex);
			if (bWritesOutput)
			{
				Parameters->DensityPressureOutput = MakeUAV(GraphBuilder, Targets.DensityPressure);
			}
			if (bWritesOutput && bWritesPainterVelocity)
			{
				Parameters->VelocityOutput = MakeUAV(GraphBuilder, Targets.Velocity);
			}
			if (bHasOutputTargets)
			{
				++InOutOutputs.WrittenOutputCount;
			}
			else
			{
				++InOutOutputs.SkippedOutputCount;
			}

			FTDFluidGpuPainterStepCS::FPermutationDomain Permutation;
			Permutation.Set<FTDFluidGpuPainterStepCS::FWritesOutputDimension>(bWritesOutput);
			Permutation.Set<FTDFluidGpuPainterStepCS::FWritesPainterVelocityDimension>(bWritesPainterVelocity);
			TShaderMapRef<FTDFluidGpuPainterStepCS> Shader(Context.ShaderMap, Permutation);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.PainterStep"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, 1));
			if (bComposesTrackBank)
			{
				AddPainterTrackBankPasses(Context, EntryIndex, Targets.DensityPressure);
			}
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

	void AddMassInputSplatPass(const FTDFluidGpuBatchPassContext& Context)
	{
		FTDFluidGpuMassInputGraph& MassInput = *Context.MassInput;
		if (MassInput.Points.IsEmpty())
		{
			return;
		}
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FTDFluidGpuMassSplatPointsCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuMassSplatPointsCS::FParameters>();
		Parameters->MassPoints = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.MassPoints"), MassInput.Points));
		Parameters->MassInputEntries = MassInput.Entries;
		Parameters->MassInputAccumulation = GraphBuilder.CreateUAV(MassInput.Accumulation, PF_R32_UINT);
		Parameters->MassInputCounters = GraphBuilder.CreateUAV(MassInput.Counters, PF_R32_UINT);
		Parameters->MassPointCount = static_cast<uint32>(MassInput.Points.Num());
		TShaderMapRef<FTDFluidGpuMassSplatPointsCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.MassSplatPoints"), Shader, Parameters, FIntVector(FMath::DivideAndRoundUp(MassInput.Points.Num(), 64), 1, 1));
	}

	void AddMassInputCounterSnapshotPasses(const FTDFluidGpuBatchPassContext& Context)
	{
		FTDFluidGpuMassInputGraph& MassInput = *Context.MassInput;
		FTDFluidGpuMassInputSnapshot* Snapshot = MassInput.Snapshot;
		if (!Snapshot || !Snapshot->Buffer)
		{
			return;
		}
		constexpr uint32 CounterBytes = sizeof(uint32) * TDFluidGpuSolverConstants::MassInputCounterCount;
		for (int32 EntryIndex = 0; EntryIndex < MassInput.CounterBases.Num(); ++EntryIndex)
		{
			const int32 SnapshotIndex = Snapshot->ActorKeys.Num();
			if (SnapshotIndex >= Snapshot->Capacity)
			{
				return;
			}
			AddCopyBufferPass(Context.GraphBuilder, Snapshot->Buffer, SnapshotIndex * CounterBytes, MassInput.Counters, MassInput.CounterBases[EntryIndex] * sizeof(uint32), CounterBytes);
			Snapshot->ActorKeys.Add(MassInput.ActorKeys[EntryIndex]);
		}
	}

	void AddMassInputClearPasses(const FTDFluidGpuBatchPassContext& Context)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		AddMassInputCounterSnapshotPasses(Context);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Context.MassInput->Accumulation, PF_R32_UINT), 0u);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Context.MassInput->Counters, PF_R32_UINT), 0u);
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
		if (Context.MassInput)
		{
			AddMassInputSplatPass(Context);
			AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("mass_splat"));
		}
		AddApplyStampsPass(Context);
		AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("apply_stamps"));
		if (Context.MassInput)
		{
			AddMassInputClearPasses(Context);
			AddPassTimestamp(Context.GraphBuilder, Context.PassTimestamps, TEXT("mass_clear"));
		}
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

	bool IsWaterHalfPressureGraph(const FTDFluidGpuBatch& Batch)
	{
		return Batch.GetKey().WaterPressureMode != ETDFluidGpuWaterPressureMode::JacobiOpenBorders;
	}

	int32 ComputeWaterGraphFieldResolution(const FTDFluidGpuBatch& Batch)
	{
		const int32 ResolutionTier = Batch.GetKey().ResolutionTier;
		return ResolutionTier / FTDFluidGpuBatch::ComputeWaterFieldDivisor(ResolutionTier);
	}

	int32 ComputeWaterGraphPressureResolution(const FTDFluidGpuBatch& Batch)
	{
		const int32 ResolutionTier = Batch.GetKey().ResolutionTier;
		return IsWaterHalfPressureGraph(Batch) ? ResolutionTier / 2 : ResolutionTier;
	}

	FTDFluidGpuBatchGraphTextures RegisterWaterBatchStateTextures(FRDGBuilder& GraphBuilder, FTDFluidGpuBatch& Batch, FTDFluidGpuWaterGraphTextures& OutWaterTextures)
	{
		const FTDFluidGpuBatchKey& Key = Batch.GetKey();
		const int32 Resolution = Key.ResolutionTier;
		const int32 FieldResolution = ComputeWaterGraphFieldResolution(Batch);
		const int32 PressureResolution = ComputeWaterGraphPressureResolution(Batch);
		const int32 SliceCapacity = FMath::Max(Batch.StateTextures.SliceCapacity, Batch.GetRequiredSliceCapacity());
		const EPixelFormat SelectedVelocityFormat = Key.bUsesFloat32WaterVelocity ? WaterVelocityFloatFormat : VelocityFormat;
		const EPixelFormat SelectedRippleFormat = Key.bUsesFloat32Ripple ? WaterRippleFloatFormat : WaterRippleHalfFormat;
		FTDFluidGpuBatchStateTextures& State = Batch.StateTextures;
		State.SurfaceDisplacement[0].SafeRelease();
		State.SurfaceDisplacement[1].SafeRelease();
		State.TerrainElevation.SafeRelease();
		State.Heat[0].SafeRelease();
		State.Heat[1].SafeRelease();
		State.PainterPrimaryDensity.SafeRelease();
		State.PainterSelectiveDensity.SafeRelease();

		FTDFluidGpuBatchGraphTextures Textures;
		Textures.Velocity[0] = RegisterOrGrowStateTexture(GraphBuilder, State.Velocity[0], Resolution, SliceCapacity, SelectedVelocityFormat, true, TEXT("TDFluidGpu.WaterVelocity"));
		Textures.Velocity[1] = RegisterOrGrowStateTexture(GraphBuilder, State.Velocity[1], Resolution, SliceCapacity, SelectedVelocityFormat, false, TEXT("TDFluidGpu.WaterVelocityScratch"));
		Textures.Density[0] = RegisterOrGrowStateTexture(GraphBuilder, State.Density[0], Resolution, SliceCapacity, DensityFormat, true, TEXT("TDFluidGpu.WaterDensity"));
		Textures.Density[1] = RegisterOrGrowStateTexture(GraphBuilder, State.Density[1], Resolution, SliceCapacity, DensityFormat, false, TEXT("TDFluidGpu.WaterDensityScratch"));
		Textures.PressureSolution[0] = RegisterOrGrowStateTexture(GraphBuilder, State.PressureSolution[0], PressureResolution, SliceCapacity, PressureSolutionFormat, true, TEXT("TDFluidGpu.WaterPressureSolution"));
		Textures.PressureSolution[1] = RegisterOrGrowStateTexture(GraphBuilder, State.PressureSolution[1], PressureResolution, SliceCapacity, PressureSolutionFormat, false, TEXT("TDFluidGpu.WaterPressureSolutionScratch"));
		Textures.ObstacleSdf = RegisterOrGrowStateTexture(GraphBuilder, State.ObstacleSdf, Resolution, SliceCapacity, ObstacleSdfFormat, true, TEXT("TDFluidGpu.WaterObstacleSdf"));
		OutWaterTextures.Ripple[0] = RegisterOrGrowStateTexture(GraphBuilder, State.Ripple[0], Resolution, SliceCapacity, SelectedRippleFormat, true, TEXT("TDFluidGpu.WaterRipple"));
		OutWaterTextures.Ripple[1] = RegisterOrGrowStateTexture(GraphBuilder, State.Ripple[1], Resolution, SliceCapacity, SelectedRippleFormat, false, TEXT("TDFluidGpu.WaterRippleScratch"));
		OutWaterTextures.GuideField = RegisterOrGrowStateTexture(GraphBuilder, State.WaterGuideField, FieldResolution, SliceCapacity, WaterGuideFieldFormat, true, TEXT("TDFluidGpu.WaterGuideField"));
		OutWaterTextures.SurfaceHeightField = RegisterOrGrowStateTexture(GraphBuilder, State.WaterSurfaceHeightField, FieldResolution, SliceCapacity, WaterScalarFieldFormat, true, TEXT("TDFluidGpu.WaterSurfaceHeightField"));
		OutWaterTextures.TerrainField = RegisterOrGrowStateTexture(GraphBuilder, State.WaterTerrainField, FieldResolution, SliceCapacity, WaterScalarFieldFormat, true, TEXT("TDFluidGpu.WaterTerrainField"));
		OutWaterTextures.Wetness = RegisterOrGrowStateTexture(GraphBuilder, State.WaterWetness, Resolution, SliceCapacity, WaterWetnessFormat, true, TEXT("TDFluidGpu.WaterWetness"));
		State.SliceCapacity = SliceCapacity;
		return Textures;
	}

	FTDFluidGpuWaterPressureBindings MakeWaterPressureBindings(const FTDFluidGpuWaterGraphTextures& Water)
	{
		FTDFluidGpuWaterPressureBindings Bindings;
		Bindings.WaterParameters = Water.WaterParameters;
		Bindings.SurfaceHeightField = Water.SurfaceHeightField;
		Bindings.OpenBorderFlags = Water.OpenBorderFlags;
		return Bindings;
	}

	void AddWaterShiftGatherPass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water, FRDGTextureRef WetnessShiftStage)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchFrame& Frame = Context.Batch.GetFrame();
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;

		FTDFluidGpuWaterShiftCS::FPermutationDomain Permutation;
		Permutation.Set<FTDFluidGpuWaterShiftCS::FPhaseDimension>(ShiftPhaseGather);

		FTDFluidGpuWaterShiftCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterShiftCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->EntryList = UploadEntryList(GraphBuilder, TEXT("TDFluidGpu.WaterShiftEntries"), TArray<uint32>(Frame.ShiftEntryIndices));
		Parameters->ShiftEntryCount = static_cast<uint32>(Frame.ShiftEntryIndices.Num());
		Parameters->VelocityIn = MakeSRV(GraphBuilder, Textures.Velocity[0]);
		Parameters->DensityIn = MakeSRV(GraphBuilder, Textures.Density[0]);
		Parameters->RippleIn = MakeSRV(GraphBuilder, Water.Ripple[0]);
		Parameters->PressureIn = MakeSRV(GraphBuilder, Textures.PressureSolution[0]);
		Parameters->WetnessIn = MakeSRV(GraphBuilder, Water.Wetness);
		Parameters->VelocityStageOut = MakeUAV(GraphBuilder, Textures.Velocity[1]);
		Parameters->DensityStageOut = MakeUAV(GraphBuilder, Textures.Density[1]);
		Parameters->RippleStageOut = MakeUAV(GraphBuilder, Water.Ripple[1]);
		Parameters->PressureStageOut = MakeUAV(GraphBuilder, Textures.PressureSolution[1]);
		Parameters->WetnessStageOut = MakeUAV(GraphBuilder, WetnessShiftStage);

		TShaderMapRef<FTDFluidGpuWaterShiftCS> Shader(Context.ShaderMap, Permutation);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterShiftGather"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, Frame.ShiftEntryIndices.Num()));
	}

	void AddWaterShiftAndResetPasses(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water)
	{
		const FTDFluidGpuBatchFrame& Frame = Context.Batch.GetFrame();
		const int32 ShiftCount = Frame.ShiftEntryIndices.Num();
		const int32 ScatterCount = ShiftCount + Frame.ResetEntryIndices.Num();
		if (ScatterCount == 0)
		{
			return;
		}

		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FRDGTextureRef WetnessShiftStage = nullptr;
		if (ShiftCount > 0)
		{
			WetnessShiftStage = GraphBuilder.CreateTexture(MakeStateArrayDesc(Context.Resolution, Context.Batch.StateTextures.SliceCapacity, WaterWetnessFormat), TEXT("TDFluidGpu.WaterWetnessShiftStage"));
			AddWaterShiftGatherPass(Context, Water, WetnessShiftStage);
		}

		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		TArray<uint32> ScatterEntries(Frame.ShiftEntryIndices);
		ScatterEntries.Append(Frame.ResetEntryIndices);

		FTDFluidGpuWaterShiftCS::FPermutationDomain Permutation;
		Permutation.Set<FTDFluidGpuWaterShiftCS::FPhaseDimension>(ShiftCount > 0 ? ShiftPhaseScatter : ShiftPhaseResetOnly);

		FTDFluidGpuWaterShiftCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterShiftCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->EntryList = UploadEntryList(GraphBuilder, TEXT("TDFluidGpu.WaterShiftScatterEntries"), MoveTemp(ScatterEntries));
		Parameters->ShiftEntryCount = static_cast<uint32>(ShiftCount);
		Parameters->VelocityOut = MakeUAV(GraphBuilder, Textures.Velocity[0]);
		Parameters->DensityOut = MakeUAV(GraphBuilder, Textures.Density[0]);
		Parameters->RippleOut = MakeUAV(GraphBuilder, Water.Ripple[0]);
		Parameters->PressureOut = MakeUAV(GraphBuilder, Textures.PressureSolution[0]);
		Parameters->WetnessOut = MakeUAV(GraphBuilder, Water.Wetness);
		Parameters->ObstacleSdfOut = MakeUAV(GraphBuilder, Textures.ObstacleSdf);
		Parameters->WaterGuideFieldOut = MakeUAV(GraphBuilder, Water.GuideField);
		Parameters->WaterSurfaceHeightFieldOut = MakeUAV(GraphBuilder, Water.SurfaceHeightField);
		Parameters->WaterTerrainFieldOut = MakeUAV(GraphBuilder, Water.TerrainField);
		if (ShiftCount > 0)
		{
			Parameters->VelocityStageIn = MakeSRV(GraphBuilder, Textures.Velocity[1]);
			Parameters->DensityStageIn = MakeSRV(GraphBuilder, Textures.Density[1]);
			Parameters->RippleStageIn = MakeSRV(GraphBuilder, Water.Ripple[1]);
			Parameters->PressureStageIn = MakeSRV(GraphBuilder, Textures.PressureSolution[1]);
			Parameters->WetnessStageIn = MakeSRV(GraphBuilder, WetnessShiftStage);
		}

		TShaderMapRef<FTDFluidGpuWaterShiftCS> Shader(Context.ShaderMap, Permutation);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterShiftScatter"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, ScatterCount));
	}

	void AddWaterGuideUpdatePass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water)
	{
		const TArray<FTDFluidGpuPackedWaterGuideUpdate>& Updates = Context.Batch.GetFrame().WaterGuideUpdates;
		if (Updates.IsEmpty())
		{
			return;
		}

		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FTDFluidGpuWaterGuideUpdateCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterGuideUpdateCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->GuideUpdates = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.WaterGuideUpdates"), Updates));
		Parameters->GuideUpdateCount = static_cast<uint32>(Updates.Num());
		Parameters->WaterGuideFieldOut = MakeUAV(GraphBuilder, Water.GuideField);
		Parameters->WaterSurfaceHeightFieldOut = MakeUAV(GraphBuilder, Water.SurfaceHeightField);

		TShaderMapRef<FTDFluidGpuWaterGuideUpdateCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterGuideUpdate"), Shader, Parameters, FComputeShaderUtils::GetGroupCount(Updates.Num(), WaterLinearGroupSize));
	}

	void AddWaterTerrainUpdatePass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water)
	{
		const TArray<FTDFluidGpuPackedWaterTerrainUpdate>& Updates = Context.Batch.GetFrame().WaterTerrainUpdates;
		if (Updates.IsEmpty())
		{
			return;
		}

		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FTDFluidGpuWaterTerrainUpdateCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterTerrainUpdateCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->TerrainUpdates = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.WaterTerrainUpdates"), Updates));
		Parameters->TerrainUpdateCount = static_cast<uint32>(Updates.Num());
		Parameters->WaterTerrainFieldOut = MakeUAV(GraphBuilder, Water.TerrainField);

		TShaderMapRef<FTDFluidGpuWaterTerrainUpdateCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterTerrainUpdate"), Shader, Parameters, FComputeShaderUtils::GetGroupCount(Updates.Num(), WaterLinearGroupSize));
	}

	void AddWaterFieldUpdatePasses(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water)
	{
		AddWaterGuideUpdatePass(Context, Water);
		AddWaterTerrainUpdatePass(Context, Water);
	}

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuWideOpenBorderScan(
		TEXT("TD.FluidFX.WaterGpuWideOpenBorderScan"),
		1,
		TEXT("GPU water scans the open border with 1024 threads in one group instead of 64. The vote is an order independent OR, so the result is bit identical. 0 keeps the 64 thread scan."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuSolidFirstReads(
		TEXT("TD.FluidFX.WaterGpuSolidFirstReads"),
		1,
		TEXT("GPU water finalize, force and ripple passes test the solid distance before they read the other state textures, so solid cells skip those reads. The values written are bit identical. 0 keeps the previous read order."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuSkipOpenPressureMean(
		TEXT("TD.FluidFX.WaterGpuSkipOpenPressureMean"),
		1,
		TEXT("GPU water pressure mean tile pass returns early for entries with open borders, because the finalize pass ignores their mean. Output and state stay bit identical. 0 keeps the previous tile pass."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuSkipRippleParityCopy(
		TEXT("TD.FluidFX.WaterGpuSkipRippleParityCopy"),
		1,
		TEXT("GPU water runs exactly the ripple substep count instead of rounding it up to an even number, and the finalize pass reads and writes the ripple textures that the last substep selects. Output and state stay bit identical. 0 keeps the even rounding."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuSkipDryPressureTiles(
		TEXT("TD.FluidFX.WaterGpuSkipDryPressureTiles"),
		1,
		TEXT("GPU water Jacobi pressure tiles without any fluid cell write zero pressure and return before loading the halo region, for grids up to 256. The pressure written is bit identical. 0 keeps the full tile work."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuFusedPressureTail(
		TEXT("TD.FluidFX.WaterGpuFusedPressureTail"),
		0,
		TEXT("GPU water Jacobi pressure solve for grids up to 256 runs the last iterations, the velocity gradient and for the second projection the pressure tile sums in one dispatch. Velocity and pressure stay bit identical, but the fused kernel measured 0.005 ms slower at 192. 1 turns the fused kernel on, 0 keeps the separate dispatches."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuFusedPrestep(
		TEXT("TD.FluidFX.WaterGpuFusedPrestep"),
		1,
		TEXT("GPU water runs the stamp, force and vorticity passes as one tiled pass, computing the stamp and force result of the two cell halo again for the vorticity. Output and state stay bit identical. Needs the float32 velocity format. 0 keeps the three separate passes."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuSharedHalfApertures(
		TEXT("TD.FluidFX.WaterGpuSharedHalfApertures"),
		1,
		TEXT("GPU water half pressure preparation computes the face apertures of an 8 by 8 group and its left and lower neighbors once in group shared memory instead of three times per cell. The values written are bit identical. 0 keeps the per cell computation."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuSingleProjection(
		TEXT("TD.FluidFX.WaterGpuSingleProjection"),
		1,
		TEXT("GPU water half pressure grids (512 and above) advect the velocity before the only projection of the step instead of projecting before and after the advection. 0 keeps the two projections."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuHalfPostSmoothBandCells(
		TEXT("TD.FluidFX.WaterGpuHalfPostSmoothBandCells"),
		14,
		TEXT("GPU water half pressure post smoothing only works on pressure tiles that hold a fluid cell within this many cells of a solid cell or an open water edge, clamped to 0..64. Other tiles keep the upsampled pressure. 0 smooths every tile."),
		ECVF_RenderThreadSafe);

	void AddWaterFieldDryPass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FTDFluidGpuWaterFieldDryCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterFieldDryCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->WaterSurfaceHeightField = MakeSRV(GraphBuilder, Water.SurfaceHeightField);
		Parameters->WaterTerrainField = MakeSRV(GraphBuilder, Water.TerrainField);
		Parameters->FieldDrySdfOut = MakeUAV(GraphBuilder, Water.FieldDrySdf);

		TShaderMapRef<FTDFluidGpuWaterFieldDryCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterFieldDry"), Shader, Parameters, MakeCellGroupCount(ComputeWaterGraphFieldResolution(Context.Batch), Context.EntryCount));
	}

	void AddWaterComposeSdfPass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FTDFluidGpuWaterComposeCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterComposeCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->ObstacleSdf = MakeSRV(GraphBuilder, Context.Textures.ObstacleSdf);
		Parameters->FieldDrySdf = MakeSRV(GraphBuilder, Water.FieldDrySdf);
		Parameters->CompositeSdfOut = MakeUAV(GraphBuilder, Water.CompositeSdf);

		FTDFluidGpuWaterComposeCS::FPermutationDomain Permutation;
		Permutation.Set<FTDFluidGpuWaterComposeCS::FInterpolatesFieldDimension>(FTDFluidGpuBatch::ComputeWaterFieldDivisor(Context.Resolution) > 1);
		TShaderMapRef<FTDFluidGpuWaterComposeCS> Shader(Context.ShaderMap, Permutation);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterCompose"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, Context.EntryCount));
	}

	void AddWaterOpenBorderScanPass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FTDFluidGpuWaterOpenBorderScanCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterOpenBorderScanCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->CompositeSdf = MakeSRV(GraphBuilder, Water.CompositeSdf);
		Parameters->WaterSurfaceHeightField = MakeSRV(GraphBuilder, Water.SurfaceHeightField);
		Parameters->WaterOpenBorderFlagsOut = GraphBuilder.CreateUAV(Water.OpenBorderFlags);

		FTDFluidGpuWaterOpenBorderScanCS::FPermutationDomain Permutation;
		Permutation.Set<FTDFluidGpuWaterOpenBorderScanCS::FWideScanDimension>(CVarTDFluidWaterGpuWideOpenBorderScan.GetValueOnAnyThread() != 0);
		TShaderMapRef<FTDFluidGpuWaterOpenBorderScanCS> Shader(Context.ShaderMap, Permutation);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterOpenBorderScan"), Shader, Parameters, FIntVector(1, 1, Context.EntryCount));
	}

	void AddWaterComposePasses(const FTDFluidGpuBatchPassContext& Context, FTDFluidGpuWaterGraphTextures& InOutWater)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const int32 SliceCapacity = Context.Batch.StateTextures.SliceCapacity;
		InOutWater.FieldDrySdf = GraphBuilder.CreateTexture(MakeStateArrayDesc(ComputeWaterGraphFieldResolution(Context.Batch), SliceCapacity, WaterFieldDrySdfFormat), TEXT("TDFluidGpu.WaterFieldDrySdf"));
		InOutWater.CompositeSdf = GraphBuilder.CreateTexture(MakeStateArrayDesc(Context.Resolution, SliceCapacity, WaterCompositeSdfFormat), TEXT("TDFluidGpu.WaterCompositeSdf"));
		InOutWater.OpenBorderFlags = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), static_cast<uint32>(Context.EntryCount)), TEXT("TDFluidGpu.WaterOpenBorderFlags"));

		AddWaterFieldDryPass(Context, InOutWater);
		AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_field_dry"));
		AddWaterComposeSdfPass(Context, InOutWater);
		AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_compose_sdf"));
		AddWaterOpenBorderScanPass(Context, InOutWater);
		AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_compose"));
	}

	void AddWaterStampsPass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		FTDFluidGpuWaterStampsCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterStampsCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->Stamps = Context.FrameResources.Stamps;
		Parameters->StampBinRanges = Context.FrameResources.StampBinRanges;
		Parameters->StampBinIndices = Context.FrameResources.StampBinIndices;
		Parameters->StampRippleImpulses = Context.FrameResources.StampRippleImpulses;
		Parameters->StampRingShapes = Context.FrameResources.StampRingShapes;
		Parameters->CompositeSdf = MakeSRV(GraphBuilder, Water.CompositeSdf);
		Parameters->WaterGuideField = MakeSRV(GraphBuilder, Water.GuideField);
		Parameters->VelocityIn = MakeSRV(GraphBuilder, Textures.Velocity[0]);
		Parameters->DensityIn = MakeSRV(GraphBuilder, Textures.Density[0]);
		Parameters->RippleIn = MakeSRV(GraphBuilder, Water.Ripple[0]);
		Parameters->VelocityOut = MakeUAV(GraphBuilder, Textures.Velocity[1]);
		Parameters->DensityOut = MakeUAV(GraphBuilder, Textures.Density[1]);
		Parameters->RippleOut = MakeUAV(GraphBuilder, Water.Ripple[1]);

		TShaderMapRef<FTDFluidGpuWaterStampsCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterStamps"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, Context.EntryCount));
	}

	void AddWaterStampsForcesVorticityPass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		FTDFluidGpuWaterStampsForcesVorticityCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterStampsForcesVorticityCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->Stamps = Context.FrameResources.Stamps;
		Parameters->StampBinRanges = Context.FrameResources.StampBinRanges;
		Parameters->StampBinIndices = Context.FrameResources.StampBinIndices;
		Parameters->StampRippleImpulses = Context.FrameResources.StampRippleImpulses;
		Parameters->StampRingShapes = Context.FrameResources.StampRingShapes;
		Parameters->CompositeSdf = MakeSRV(GraphBuilder, Water.CompositeSdf);
		Parameters->WaterGuideField = MakeSRV(GraphBuilder, Water.GuideField);
		Parameters->WaterSurfaceHeightField = MakeSRV(GraphBuilder, Water.SurfaceHeightField);
		Parameters->WaterTerrainField = MakeSRV(GraphBuilder, Water.TerrainField);
		Parameters->VelocityIn = MakeSRV(GraphBuilder, Textures.Velocity[0]);
		Parameters->DensityIn = MakeSRV(GraphBuilder, Textures.Density[0]);
		Parameters->RippleIn = MakeSRV(GraphBuilder, Water.Ripple[0]);
		Parameters->VelocityOut = MakeUAV(GraphBuilder, Textures.Velocity[1]);
		Parameters->DensityOut = MakeUAV(GraphBuilder, Textures.Density[1]);
		Parameters->RippleOut = MakeUAV(GraphBuilder, Water.Ripple[1]);

		TShaderMapRef<FTDFluidGpuWaterStampsForcesVorticityCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterStampsForcesVorticity"), Shader, Parameters, MakeTileGroupCount(Context.Resolution, Context.EntryCount));
	}

	void AddWaterForcesPass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		FTDFluidGpuWaterForcesCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterForcesCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->CompositeSdf = MakeSRV(GraphBuilder, Water.CompositeSdf);
		Parameters->WaterGuideField = MakeSRV(GraphBuilder, Water.GuideField);
		Parameters->WaterSurfaceHeightField = MakeSRV(GraphBuilder, Water.SurfaceHeightField);
		Parameters->WaterTerrainField = MakeSRV(GraphBuilder, Water.TerrainField);
		Parameters->VelocityIn = MakeSRV(GraphBuilder, Textures.Velocity[1]);
		Parameters->DensityIn = MakeSRV(GraphBuilder, Textures.Density[1]);
		Parameters->VelocityOut = MakeUAV(GraphBuilder, Textures.Velocity[0]);

		FTDFluidGpuWaterForcesCS::FPermutationDomain Permutation;
		Permutation.Set<FTDFluidGpuWaterForcesCS::FSolidFirstReadsDimension>(CVarTDFluidWaterGpuSolidFirstReads.GetValueOnAnyThread() != 0);
		TShaderMapRef<FTDFluidGpuWaterForcesCS> Shader(Context.ShaderMap, Permutation);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterForces"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, Context.EntryCount));
	}

	FRDGTextureRef GetWaterHalfPressureCorrection(const FTDFluidGpuMultigridLevels& Levels)
	{
		const FTDFluidGpuMultigridLevelTextures& HalfLevel = Levels[WaterHalfPressureLevel];
		return Levels.Num() - 1 > WaterHalfPressureLevel ? HalfLevel.Correction[1] : HalfLevel.Correction[0];
	}

	void AddWaterHalfPressurePreparePass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water, const FTDFluidGpuMultigridLevels& Levels, FRDGTextureRef ProjectedVelocity, FRDGTextureRef HalfPressure, float ClosedWarmStartScale)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuMultigridLevelTextures& HalfLevel = Levels[WaterHalfPressureLevel];
		FTDFluidGpuWaterHalfPressurePrepareCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterHalfPressurePrepareCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->CompositeSdf = MakeSRV(GraphBuilder, Water.CompositeSdf);
		Parameters->WaterSurfaceHeightField = MakeSRV(GraphBuilder, Water.SurfaceHeightField);
		Parameters->WaterOpenBorderFlags = GraphBuilder.CreateSRV(Water.OpenBorderFlags);
		Parameters->VelocityIn = MakeSRV(GraphBuilder, ProjectedVelocity);
		Parameters->PressureIn = MakeSRV(GraphBuilder, HalfPressure);
		Parameters->ClosedWarmStartScale = ClosedWarmStartScale;
		Parameters->OpenBoundaryDirichletScale = ComputeWaterDirichletScale(WaterHalfPressureLevel);
		Parameters->RhsOut = MakeUAV(GraphBuilder, HalfLevel.Rhs);
		Parameters->AperturesOut = MakeUAV(GraphBuilder, HalfLevel.Apertures);
		Parameters->OpenBoundaryOut = MakeUAV(GraphBuilder, HalfLevel.OpenBoundary);

		FTDFluidGpuWaterHalfPressurePrepareCS::FPermutationDomain Permutation;
		Permutation.Set<FTDFluidGpuWaterHalfPressurePrepareCS::FSharedAperturesDimension>(CVarTDFluidWaterGpuSharedHalfApertures.GetValueOnAnyThread() != 0);
		TShaderMapRef<FTDFluidGpuWaterHalfPressurePrepareCS> Shader(Context.ShaderMap, Permutation);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterHalfPressurePrepare"), Shader, Parameters, MakeCellGroupCount(Context.Resolution / 2, Context.EntryCount));
	}

	void AddWaterHalfPressureApplyPass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water, const FTDFluidGpuMultigridLevels& Levels, FRDGTextureRef HalfPressure, float ClosedWarmStartScale)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FTDFluidGpuWaterHalfPressureApplyCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterHalfPressureApplyCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->CorrectionIn = MakeSRV(GraphBuilder, GetWaterHalfPressureCorrection(Levels));
		Parameters->WaterOpenBorderFlags = GraphBuilder.CreateSRV(Water.OpenBorderFlags);
		Parameters->ClosedWarmStartScale = ClosedWarmStartScale;
		Parameters->PressureSolution = MakeUAV(GraphBuilder, HalfPressure);

		TShaderMapRef<FTDFluidGpuWaterHalfPressureApplyCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterHalfPressureApply"), Shader, Parameters, MakeCellGroupCount(Context.Resolution / 2, Context.EntryCount));
	}

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuClosedHalfRhsMean(
		TEXT("TD.FluidFX.WaterGpuClosedHalfRhsMean"),
		1,
		TEXT("GPU water half resolution multigrid subtracts the mean of the half resolution right hand side for entries without open borders, which keeps the closed pocket problem compatible. 0 keeps the previous behavior."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarTDFluidWaterGpuHalfPostSmoothIterations(
		TEXT("TD.FluidFX.WaterGpuHalfPostSmoothIterations"),
		4,
		TEXT("Full resolution Jacobi iterations applied to the upsampled half resolution pressure before the second projection gradient, clamped to 0..16. 0 keeps the previous behavior."),
		ECVF_RenderThreadSafe);

	FRDGBufferRef AddWaterPressureMeanPasses(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water, FRDGTextureRef Pressure)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const int32 TilesPerRow = ComputePressureMeanTilesPerRow(Context.Resolution);
		FRDGBufferRef PartialSums = CreatePressurePartialSums(GraphBuilder, Context.Resolution, Context.EntryCount);
		FTDFluidGpuWaterPressureMeanTilesCS::FParameters* TileParameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterPressureMeanTilesCS::FParameters>();
		TileParameters->ActorParameters = Context.ActorParameters;
		TileParameters->WaterParameters = Water.WaterParameters;
		TileParameters->CompositeSdf = MakeSRV(GraphBuilder, Water.CompositeSdf);
		TileParameters->PressureIn = MakeSRV(GraphBuilder, Pressure);
		TileParameters->WaterOpenBorderFlags = GraphBuilder.CreateSRV(Water.OpenBorderFlags);
		TileParameters->WaterPressurePartialSumsOut = GraphBuilder.CreateUAV(PartialSums);
		TileParameters->TilesPerSlice = static_cast<uint32>(TilesPerRow * TilesPerRow);
		TileParameters->TilesPerRow = static_cast<uint32>(TilesPerRow);
		TShaderMapRef<FTDFluidGpuWaterPressureMeanTilesCS> TileShader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterPressureMeanTiles"), TileShader, TileParameters, MakeTileGroupCount(Context.Resolution, Context.EntryCount));
		return AddPressureMeanPasses(Context, Pressure, PartialSums);
	}

	bool HasClosedWaterEntry(const FTDFluidGpuBatch& Batch)
	{
		for (const FTDFluidGpuWaterParameters& WaterParameters : Batch.GetFrame().WaterParameters)
		{
			if (!EnumHasAnyFlags(static_cast<ETDFluidGpuWaterFlags>(WaterParameters.WaterFlags), ETDFluidGpuWaterFlags::UsesOpenBorders))
			{
				return true;
			}
		}
		return false;
	}

	void AddWaterHalfRhsMeanPasses(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water, const FTDFluidGpuMultigridLevels& Levels)
	{
		if (CVarTDFluidWaterGpuClosedHalfRhsMean.GetValueOnAnyThread() == 0 || !HasClosedWaterEntry(Context.Batch))
		{
			return;
		}

		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const int32 HalfResolution = Context.Resolution / 2;
		const int32 TilesPerRow = ComputePressureMeanTilesPerRow(HalfResolution);
		const int32 TilesPerSlice = TilesPerRow * TilesPerRow;
		FRDGTextureRef HalfRhs = Levels[WaterHalfPressureLevel].Rhs;
		FRDGBufferRef PartialSums = CreatePressurePartialSums(GraphBuilder, HalfResolution, Context.EntryCount);
		FRDGBufferRef MeanRhs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), static_cast<uint32>(Context.EntryCount)), TEXT("TDFluidGpu.WaterHalfRhsMean"));

		FTDFluidGpuWaterHalfRhsMeanTilesCS::FParameters* TileParameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterHalfRhsMeanTilesCS::FParameters>();
		TileParameters->ActorParameters = Context.ActorParameters;
		TileParameters->WaterParameters = Water.WaterParameters;
		TileParameters->CompositeSdf = MakeSRV(GraphBuilder, Water.CompositeSdf);
		TileParameters->HalfRhsIn = MakeSRV(GraphBuilder, HalfRhs);
		TileParameters->HalfRhsPartialSumsOut = GraphBuilder.CreateUAV(PartialSums);
		TileParameters->TilesPerSlice = static_cast<uint32>(TilesPerSlice);
		TileParameters->TilesPerRow = static_cast<uint32>(TilesPerRow);
		TShaderMapRef<FTDFluidGpuWaterHalfRhsMeanTilesCS> TileShader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterHalfRhsMeanTiles"), TileShader, TileParameters, MakeTileGroupCount(HalfResolution, Context.EntryCount));

		FTDFluidGpuPressureMeanCS::FParameters* FinalParameters = GraphBuilder.AllocParameters<FTDFluidGpuPressureMeanCS::FParameters>();
		FinalParameters->PressurePartialSumsIn = GraphBuilder.CreateSRV(PartialSums);
		FinalParameters->MeanPressureOut = GraphBuilder.CreateUAV(MeanRhs);
		FinalParameters->TilesPerSlice = static_cast<uint32>(TilesPerSlice);
		FinalParameters->TilesPerRow = static_cast<uint32>(TilesPerRow);
		TShaderMapRef<FTDFluidGpuPressureMeanCS> FinalShader(Context.ShaderMap, FTDFluidGpuPressureMeanCS::FPermutationDomain(1));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterHalfRhsMeanFinal"), FinalShader, FinalParameters, FIntVector(1, 1, Context.EntryCount));

		FTDFluidGpuWaterHalfRhsSubtractMeanCS::FParameters* SubtractParameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterHalfRhsSubtractMeanCS::FParameters>();
		SubtractParameters->ActorParameters = Context.ActorParameters;
		SubtractParameters->WaterParameters = Water.WaterParameters;
		SubtractParameters->CompositeSdf = MakeSRV(GraphBuilder, Water.CompositeSdf);
		SubtractParameters->WaterOpenBorderFlags = GraphBuilder.CreateSRV(Water.OpenBorderFlags);
		SubtractParameters->HalfRhsMeanIn = GraphBuilder.CreateSRV(MeanRhs);
		SubtractParameters->HalfRhsInOut = MakeUAV(GraphBuilder, HalfRhs);
		TShaderMapRef<FTDFluidGpuWaterHalfRhsSubtractMeanCS> SubtractShader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterHalfRhsSubtractMean"), SubtractShader, SubtractParameters, MakeCellGroupCount(HalfResolution, Context.EntryCount));
	}

	void AddWaterHalfPressureCyclePasses(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water, const FTDFluidGpuMultigridPassContext& MultigridContext, const FTDFluidGpuMultigridLevels& Levels, FRDGTextureRef ProjectedVelocity, FRDGTextureRef HalfPressure, float ClosedWarmStartScale, bool bWritesCoarseApertures)
	{
		AddWaterHalfPressurePreparePass(Context, Water, Levels, ProjectedVelocity, HalfPressure, ClosedWarmStartScale);
		AddWaterHalfRhsMeanPasses(Context, Water, Levels);
		AddWaterMultigridCyclePasses(MultigridContext, Levels, bWritesCoarseApertures);
		AddWaterHalfPressureApplyPass(Context, Water, Levels, HalfPressure, ClosedWarmStartScale);
	}

	FRDGTextureRef AddWaterUpsamplePressurePass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water, const FTDFluidGpuMultigridLevels& Levels, FRDGTextureRef HalfPressure)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FRDGTextureRef PressureFull = GraphBuilder.CreateTexture(MakeStateArrayDesc(Context.Resolution, Context.Batch.StateTextures.SliceCapacity, WaterUpsampledPressureFormat), TEXT("TDFluidGpu.WaterPressureFull"));
		FTDFluidGpuWaterUpsamplePressureCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterUpsamplePressureCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->PressureIn = MakeSRV(GraphBuilder, HalfPressure);
		Parameters->AperturesIn = MakeSRV(GraphBuilder, Levels[WaterHalfPressureLevel].Apertures);
		Parameters->CompositeSdf = MakeSRV(GraphBuilder, Water.CompositeSdf);
		Parameters->WaterSurfaceHeightField = MakeSRV(GraphBuilder, Water.SurfaceHeightField);
		Parameters->WaterOpenBorderFlags = GraphBuilder.CreateSRV(Water.OpenBorderFlags);
		Parameters->PressureFullOut = MakeUAV(GraphBuilder, PressureFull);

		TShaderMapRef<FTDFluidGpuWaterUpsamplePressureCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterUpsamplePressure"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, Context.EntryCount));
		return PressureFull;
	}

	FRDGTextureRef AddWaterHalfPostSmoothPasses(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water, FRDGTextureRef ProjectedVelocity, FRDGTextureRef UpsampledPressure)
	{
		const int32 IterationCount = ComputeTDFluidGpuWaterHalfPostSmoothIterations();
		if (IterationCount == 0)
		{
			return UpsampledPressure;
		}

		RDG_EVENT_SCOPE_STAT(Context.GraphBuilder, TDFluidGpuWaterPressure, "TDFluidGpuWaterPressure");
		FRDGTextureRef SmoothedPressure = Context.GraphBuilder.CreateTexture(MakeStateArrayDesc(Context.Resolution, Context.Batch.StateTextures.SliceCapacity, WaterUpsampledPressureFormat), TEXT("TDFluidGpu.WaterPressureFullSmoothed"));
		FRDGTextureRef PressureTextures[2] = { UpsampledPressure, SmoothedPressure };
		const FTDFluidGpuWaterPressureBindings WaterBindings = MakeWaterPressureBindings(Water);
		const int32 BandCells = FMath::Clamp(CVarTDFluidWaterGpuHalfPostSmoothBandCells.GetValueOnAnyThread(), 0, 64);
		FTDFluidGpuJacobiTileSkipOptions TileSkip;
		TileSkip.bSkipsTiles = BandCells > 0;
		TileSkip.BandCells = static_cast<float>(BandCells);
		const int32 SmoothedPressureIndex = AddJacobiPassesOnTextures(Context, ProjectedVelocity, PressureTextures, 0, IterationCount, &WaterBindings, TileSkip);
		return PressureTextures[SmoothedPressureIndex];
	}

	FRDGTextureRef AddWaterPressurePasses(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water, const FTDFluidGpuMultigridLevels& Levels, FRDGTextureRef ProjectedVelocity, int32& InOutPressureIndex, bool bIsFirstProjection, const FTDFluidGpuJacobiFusedTail* FusedTail = nullptr)
	{
		RDG_EVENT_SCOPE_STAT(Context.GraphBuilder, TDFluidGpuWaterPressure, "TDFluidGpuWaterPressure");
		if (!IsWaterHalfPressureGraph(Context.Batch))
		{
			const FTDFluidGpuWaterPressureBindings WaterBindings = MakeWaterPressureBindings(Water);
			FTDFluidGpuJacobiTileSkipOptions TileSkip;
			TileSkip.bSkipsTiles = CVarTDFluidWaterGpuSkipDryPressureTiles.GetValueOnAnyThread() != 0;
			InOutPressureIndex = AddJacobiPasses(Context, ProjectedVelocity, InOutPressureIndex, &WaterBindings, TileSkip, FusedTail);
			return Context.Textures.PressureSolution[InOutPressureIndex];
		}

		const FTDFluidGpuMultigridPassContext MultigridContext = MakeWaterMultigridPassContext(Context);
		FRDGTextureRef HalfPressure = Context.Textures.PressureSolution[InOutPressureIndex];
		const int32 CycleCount = FMath::Max(Context.Batch.GetKey().MultigridCycles, 1);
		const float FirstCycleWarmStartScale = bIsFirstProjection ? ComputeMultigridWarmStartScale() : 1.0f;
		for (int32 CycleIndex = 0; CycleIndex < CycleCount; ++CycleIndex)
		{
			AddWaterHalfPressureCyclePasses(Context, Water, MultigridContext, Levels, ProjectedVelocity, HalfPressure, CycleIndex == 0 ? FirstCycleWarmStartScale : 1.0f, bIsFirstProjection && CycleIndex == 0);
		}
		return AddWaterUpsamplePressurePass(Context, Water, Levels, HalfPressure);
	}

	void AddWaterAdvectVelocityPass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water, FRDGTextureRef VelocityInTexture, FRDGTextureRef VelocityOutTexture)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FTDFluidGpuWaterAdvectVelocityCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterAdvectVelocityCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->CompositeSdf = MakeSRV(GraphBuilder, Water.CompositeSdf);
		Parameters->WaterGuideField = MakeSRV(GraphBuilder, Water.GuideField);
		Parameters->VelocityIn = MakeSRV(GraphBuilder, VelocityInTexture);
		Parameters->VelocityOut = MakeUAV(GraphBuilder, VelocityOutTexture);

		TShaderMapRef<FTDFluidGpuWaterAdvectVelocityCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterAdvectVelocity"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, Context.EntryCount));
	}

	int32 ComputeWaterRippleDispatchCount(const FTDFluidGpuBatch& Batch)
	{
		const int32 SubstepMaximum = FMath::Max(Batch.GetFrame().WaterRippleSubstepMaximum, 0);
		if (CVarTDFluidWaterGpuSkipRippleParityCopy.GetValueOnAnyThread() != 0)
		{
			return SubstepMaximum;
		}

		return SubstepMaximum + SubstepMaximum % 2;
	}

	void AddWaterRipplePasses(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water, int32 DispatchCount)
	{
		if (DispatchCount == 0)
		{
			return;
		}

		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		FRDGTextureSRVRef CompositeSdfSRV = MakeSRV(GraphBuilder, Water.CompositeSdf);
		FTDFluidGpuWaterRippleSubstepCS::FPermutationDomain Permutation;
		Permutation.Set<FTDFluidGpuWaterRippleSubstepCS::FSolidFirstReadsDimension>(CVarTDFluidWaterGpuSolidFirstReads.GetValueOnAnyThread() != 0);
		TShaderMapRef<FTDFluidGpuWaterRippleSubstepCS> Shader(Context.ShaderMap, Permutation);
		for (int32 SubstepIndex = 0; SubstepIndex < DispatchCount; ++SubstepIndex)
		{
			const bool bReadsScratch = SubstepIndex % 2 == 0;
			FTDFluidGpuWaterRippleSubstepCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterRippleSubstepCS::FParameters>();
			Parameters->ActorParameters = Context.ActorParameters;
			Parameters->WaterParameters = Water.WaterParameters;
			Parameters->CompositeSdf = CompositeSdfSRV;
			Parameters->RippleIn = MakeSRV(GraphBuilder, bReadsScratch ? Water.Ripple[1] : Water.Ripple[0]);
			Parameters->RippleOut = MakeUAV(GraphBuilder, bReadsScratch ? Water.Ripple[0] : Water.Ripple[1]);
			Parameters->SubstepIndex = static_cast<uint32>(SubstepIndex);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterRippleSubstep %d", SubstepIndex), Shader, Parameters, MakeCellGroupCount(Context.Resolution, Context.EntryCount));
		}
	}

	FTDFluidGpuWaterOutputTargets RegisterWaterOutputTargets(FRDGBuilder& GraphBuilder, const FTDFluidGpuActorInput& Actor, int32 Resolution)
	{
		FTDFluidGpuWaterOutputTargets Targets;
		FRDGTextureRef DensityPressureTarget = RegisterOutputTexture(GraphBuilder, Actor.DensityPressureOutput, Resolution, TEXT("TDFluidGpu.DensityPressureOutput"));
		if (!DensityPressureTarget)
		{
			return Targets;
		}

		FRDGTextureRef VelocityTarget = RegisterOutputTexture(GraphBuilder, Actor.VelocityOutput, Resolution, TEXT("TDFluidGpu.VelocityOutput"));
		if (!VelocityTarget)
		{
			return Targets;
		}

		FRDGTextureRef WaterSurfaceTarget = RegisterOutputTexture(GraphBuilder, Actor.WaterSurfaceOutput, Resolution, TEXT("TDFluidGpu.WaterSurfaceOutput"));
		if (!WaterSurfaceTarget)
		{
			return Targets;
		}

		Targets.DensityPressure = DensityPressureTarget;
		Targets.Velocity = VelocityTarget;
		Targets.WaterSurface = WaterSurfaceTarget;
		return Targets;
	}

	void AddWaterFinalizePasses(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water, FRDGTextureRef PressureFull, int32 PressureIndex, int32 FinalRippleIndex, FRDGBufferRef MeanPressure, FTDFluidGpuFrameOutputs& InOutOutputs)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		const FTDFluidGpuBatchFrame& Frame = Context.Batch.GetFrame();
		const bool bUsesHalfPressure = PressureFull != nullptr;
		const bool bReadsSolidCellsFirst = CVarTDFluidWaterGpuSolidFirstReads.GetValueOnAnyThread() != 0;
		FRDGBufferSRVRef MeanPressureSRV = GraphBuilder.CreateSRV(MeanPressure);
		FRDGBufferSRVRef OpenBorderFlagsSRV = GraphBuilder.CreateSRV(Water.OpenBorderFlags);
		FRDGTextureSRVRef CompositeSdfSRV = MakeSRV(GraphBuilder, Water.CompositeSdf);
		FRDGTextureSRVRef GuideFieldSRV = MakeSRV(GraphBuilder, Water.GuideField);
		FRDGTextureSRVRef SurfaceHeightFieldSRV = MakeSRV(GraphBuilder, Water.SurfaceHeightField);
		FRDGTextureSRVRef TerrainFieldSRV = MakeSRV(GraphBuilder, Water.TerrainField);
		FRDGTextureSRVRef VelocitySRV = MakeSRV(GraphBuilder, Textures.Velocity[0]);
		FRDGTextureSRVRef DensitySRV = MakeSRV(GraphBuilder, Textures.Density[1]);
		FRDGTextureSRVRef RippleSRV = MakeSRV(GraphBuilder, Water.Ripple[1 - FinalRippleIndex]);
		FRDGTextureSRVRef PressureFullSRV = bUsesHalfPressure ? MakeSRV(GraphBuilder, PressureFull) : nullptr;
		FRDGTextureUAVRef DensityUAV = MakeUnorderedUAV(GraphBuilder, Textures.Density[0]);
		FRDGTextureUAVRef RippleUAV = MakeUnorderedUAV(GraphBuilder, Water.Ripple[FinalRippleIndex]);
		FRDGTextureUAVRef PressureUAV = MakeUnorderedUAV(GraphBuilder, Textures.PressureSolution[PressureIndex]);
		FRDGTextureUAVRef WetnessUAV = MakeUnorderedUAV(GraphBuilder, Water.Wetness);

		for (int32 EntryIndex = 0; EntryIndex < Context.EntryCount; ++EntryIndex)
		{
			const FTDFluidGpuActorInput& Actor = Context.FrameInputs.Actors[Frame.Entries[EntryIndex].ActorInputIndex];
			const FTDFluidGpuWaterOutputTargets Targets = RegisterWaterOutputTargets(GraphBuilder, Actor, Context.Resolution);
			const bool bWritesOutput = Targets.DensityPressure != nullptr;

			FTDFluidGpuWaterFinalizeCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterFinalizeCS::FParameters>();
			Parameters->ActorParameters = Context.ActorParameters;
			Parameters->WaterParameters = Water.WaterParameters;
			Parameters->CompositeSdf = CompositeSdfSRV;
			Parameters->WaterGuideField = GuideFieldSRV;
			Parameters->WaterSurfaceHeightField = SurfaceHeightFieldSRV;
			Parameters->WaterTerrainField = TerrainFieldSRV;
			Parameters->WaterOpenBorderFlags = OpenBorderFlagsSRV;
			Parameters->MeanPressure = MeanPressureSRV;
			Parameters->VelocityIn = VelocitySRV;
			Parameters->DensityIn = DensitySRV;
			Parameters->RippleIn = RippleSRV;
			Parameters->DensityOut = DensityUAV;
			Parameters->RippleOut = RippleUAV;
			Parameters->PressureSolution = PressureUAV;
			Parameters->WetnessState = WetnessUAV;
			Parameters->EntryIndex = static_cast<uint32>(EntryIndex);
			Parameters->PressureFull = PressureFullSRV;
			if (bWritesOutput)
			{
				Parameters->DensityPressureOutput = MakeUAV(GraphBuilder, Targets.DensityPressure);
				Parameters->VelocityOutput = MakeUAV(GraphBuilder, Targets.Velocity);
				Parameters->WaterSurfaceOutput = MakeUAV(GraphBuilder, Targets.WaterSurface);
				++InOutOutputs.WrittenOutputCount;
			}
			else
			{
				++InOutOutputs.SkippedOutputCount;
			}

			FTDFluidGpuWaterFinalizeCS::FPermutationDomain Permutation;
			Permutation.Set<FTDFluidGpuWaterFinalizeCS::FWritesOutputDimension>(bWritesOutput);
			Permutation.Set<FTDFluidGpuWaterFinalizeCS::FHalfPressureDimension>(bUsesHalfPressure);
			Permutation.Set<FTDFluidGpuWaterFinalizeCS::FSolidFirstReadsDimension>(bReadsSolidCellsFirst);
			TShaderMapRef<FTDFluidGpuWaterFinalizeCS> Shader(Context.ShaderMap, Permutation);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterFinalize"), Shader, Parameters, MakeCellGroupCount(Context.Resolution, 1));
		}
	}

	void AddWaterProbePass(const FTDFluidGpuBatchPassContext& Context, const FTDFluidGpuWaterGraphTextures& Water, int32 FinalRippleIndex)
	{
		const TArray<FTDFluidGpuWaterProbeRequest>& Requests = Context.Batch.GetFrame().WaterProbeRequests;
		if (Requests.IsEmpty() || !Context.FrameResources.WaterProbeSamples)
		{
			return;
		}

		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchGraphTextures& Textures = Context.Textures;
		FTDFluidGpuWaterProbeCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterProbeCS::FParameters>();
		Parameters->ActorParameters = Context.ActorParameters;
		Parameters->WaterParameters = Water.WaterParameters;
		Parameters->ProbeRequests = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.WaterProbeRequests"), Requests));
		Parameters->ProbeRequestCount = static_cast<uint32>(Requests.Num());
		Parameters->CompositeSdf = MakeSRV(GraphBuilder, Water.CompositeSdf);
		Parameters->WaterGuideField = MakeSRV(GraphBuilder, Water.GuideField);
		Parameters->WaterSurfaceHeightField = MakeSRV(GraphBuilder, Water.SurfaceHeightField);
		Parameters->WaterTerrainField = MakeSRV(GraphBuilder, Water.TerrainField);
		Parameters->VelocityIn = MakeSRV(GraphBuilder, Textures.Velocity[0]);
		Parameters->DensityIn = MakeSRV(GraphBuilder, Textures.Density[0]);
		Parameters->RippleIn = MakeSRV(GraphBuilder, Water.Ripple[FinalRippleIndex]);
		Parameters->WetnessIn = MakeSRV(GraphBuilder, Water.Wetness);
		Parameters->ProbeSamplesOut = GraphBuilder.CreateUAV(Context.FrameResources.WaterProbeSamples);

		TShaderMapRef<FTDFluidGpuWaterProbeCS> Shader(Context.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterProbe"), Shader, Parameters, FComputeShaderUtils::GetGroupCount(Requests.Num(), WaterLinearGroupSize));
	}

	int32 AddWaterBatchPasses(const FTDFluidGpuBatchPassContext& Context, FTDFluidGpuWaterGraphTextures& Water, FTDFluidGpuFrameOutputs& InOutOutputs, bool& bOutSwapsVelocityStates)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const int32 RippleDispatchCount = ComputeWaterRippleDispatchCount(Context.Batch);
		const int32 FinalRippleIndex = RippleDispatchCount % 2;
		AddWaterShiftAndResetPasses(Context, Water);
		AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_shift"));
		AddRasterizeObstaclesPass(Context);
		AddWaterFieldUpdatePasses(Context, Water);
		AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_fields"));
		AddWaterComposePasses(Context, Water);

		FTDFluidGpuBatchGraphTextures SolverTextures = Context.Textures;
		SolverTextures.ObstacleSdf = Water.CompositeSdf;
		const FTDFluidGpuBatchPassContext SolverContext(GraphBuilder, Context.ShaderMap, Context.FrameInputs, Context.FrameResources, Context.Batch, SolverTextures, Context.ActorParameters, Context.Obstacles, Context.PassTimestamps);
		if (CVarTDFluidWaterGpuFusedPrestep.GetValueOnAnyThread() != 0 && Context.Batch.GetKey().bUsesFloat32WaterVelocity)
		{
			AddWaterStampsForcesVorticityPass(SolverContext, Water);
			AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_stamps"));
			AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_forces"));
			AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("vorticity"));
		}
		else
		{
			AddWaterStampsPass(SolverContext, Water);
			AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_stamps"));
			AddWaterForcesPass(SolverContext, Water);
			AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_forces"));
			AddVorticityPass(SolverContext);
			AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("vorticity"));
		}

		const bool bUsesHalfPressure = IsWaterHalfPressureGraph(Context.Batch);
		const bool bUsesSingleProjection = bUsesHalfPressure && CVarTDFluidWaterGpuSingleProjection.GetValueOnAnyThread() != 0;
		const bool bFusesPressureTail = !bUsesHalfPressure && CVarTDFluidWaterGpuFusedPressureTail.GetValueOnAnyThread() != 0 && Context.Batch.GetKey().PressureIterations > JacobiIterationsPerDispatch;
		const FTDFluidGpuMultigridLevels MultigridLevels = bUsesHalfPressure ? CreateMultigridLevels(MakeWaterMultigridPassContext(SolverContext)) : FTDFluidGpuMultigridLevels();
		const FTDFluidGpuWaterPressureBindings WaterBindings = MakeWaterPressureBindings(Water);
		int32 PressureIndex = 0;
		FRDGTextureRef ProjectionInput = SolverTextures.Velocity[1];
		FRDGTextureRef ProjectionOutput = SolverTextures.Velocity[0];
		if (bUsesSingleProjection)
		{
			ProjectionInput = SolverTextures.Velocity[0];
			ProjectionOutput = SolverTextures.Velocity[1];
			AddWaterAdvectVelocityPass(SolverContext, Water, SolverTextures.Velocity[1], SolverTextures.Velocity[0]);
			AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_advect"));
		}
		else
		{
			FTDFluidGpuJacobiFusedTail FirstTail;
			FirstTail.VelocityOut = SolverTextures.Velocity[0];
			FRDGTextureRef FirstPressure = AddWaterPressurePasses(SolverContext, Water, MultigridLevels, SolverTextures.Velocity[1], PressureIndex, true, bFusesPressureTail ? &FirstTail : nullptr);
			AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_pressure1"));
			if (!bFusesPressureTail)
			{
				AddSubtractGradientPass(SolverContext, FirstPressure, SolverTextures.Velocity[1], SolverTextures.Velocity[0], false, &WaterBindings);
			}
			AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("subtract_gradient"));
			AddWaterAdvectVelocityPass(SolverContext, Water, SolverTextures.Velocity[0], SolverTextures.Velocity[1]);
			AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_advect"));
		}

		FRDGBufferRef PressurePartialSums = bFusesPressureTail ? CreatePressurePartialSums(GraphBuilder, Context.Resolution, Context.EntryCount) : nullptr;
		FTDFluidGpuJacobiFusedTail SecondTail;
		SecondTail.VelocityOut = ProjectionOutput;
		SecondTail.PressurePartialSums = PressurePartialSums;
		SecondTail.bEnforcesObstacleBoundaries = true;
		FRDGTextureRef SecondPressure = AddWaterPressurePasses(SolverContext, Water, MultigridLevels, ProjectionInput, PressureIndex, bUsesSingleProjection, bFusesPressureTail ? &SecondTail : nullptr);
		AddPassTimestamp(GraphBuilder, Context.PassTimestamps, bUsesSingleProjection ? TEXT("water_pressure1") : TEXT("water_pressure2"));
		FRDGBufferRef MeanPressure = nullptr;
		if (bFusesPressureTail)
		{
			MeanPressure = AddPressureMeanPasses(SolverContext, SecondPressure, PressurePartialSums);
		}
		else
		{
			MeanPressure = CVarTDFluidWaterGpuSkipOpenPressureMean.GetValueOnAnyThread() != 0 ? AddWaterPressureMeanPasses(SolverContext, Water, SecondPressure) : AddPressureMeanPasses(SolverContext, SecondPressure, nullptr);
		}
		AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("pressure_mean"));
		FRDGTextureRef GradientPressure = bUsesHalfPressure ? AddWaterHalfPostSmoothPasses(SolverContext, Water, ProjectionInput, SecondPressure) : SecondPressure;
		if (bUsesHalfPressure && ComputeTDFluidGpuWaterHalfPostSmoothIterations() > 0)
		{
			AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_post_smooth"));
		}
		if (!bFusesPressureTail)
		{
			AddSubtractGradientPass(SolverContext, GradientPressure, ProjectionInput, ProjectionOutput, true, &WaterBindings);
		}
		AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("subtract_gradient"));
		AddWaterRipplePasses(SolverContext, Water, RippleDispatchCount);
		AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_ripple"));

		FTDFluidGpuBatchGraphTextures FinalTextures = SolverTextures;
		if (bUsesSingleProjection)
		{
			Swap(FinalTextures.Velocity[0], FinalTextures.Velocity[1]);
		}
		const FTDFluidGpuBatchPassContext FinalContext(GraphBuilder, Context.ShaderMap, Context.FrameInputs, Context.FrameResources, Context.Batch, FinalTextures, Context.ActorParameters, Context.Obstacles, Context.PassTimestamps);
		AddWaterFinalizePasses(FinalContext, Water, bUsesHalfPressure ? GradientPressure : nullptr, PressureIndex, FinalRippleIndex, MeanPressure, InOutOutputs);
		AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_finalize"));
		AddReadbackDownsamplePass(FinalContext);
		AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("readback_downsample"));
		AddWaterProbePass(FinalContext, Water, FinalRippleIndex);
		AddPassTimestamp(GraphBuilder, Context.PassTimestamps, TEXT("water_probe"));
		bOutSwapsVelocityStates = bUsesSingleProjection;
		return FinalRippleIndex;
	}

	void AddWaterObstacleDistanceAuxPasses(const FTDFluidGpuBatchPassContext& Context)
	{
		FRDGBuilder& GraphBuilder = Context.GraphBuilder;
		const FTDFluidGpuBatchFrame& Frame = Context.Batch.GetFrame();
		FRDGTextureSRVRef ObstacleSdfSRV = nullptr;
		for (int32 EntryIndex = 0; EntryIndex < Context.EntryCount; ++EntryIndex)
		{
			const FTDFluidGpuActorInput& Actor = Context.FrameInputs.Actors[Frame.Entries[EntryIndex].ActorInputIndex];
			if (!Actor.AuxOutput || !Actor.Settings.bWritesObstacleDistanceAux || !Actor.bRefreshesObstacleDistanceAux)
			{
				continue;
			}

			FRDGTextureRef AuxTarget = RegisterOutputTexture(GraphBuilder, Actor.AuxOutput, Context.Resolution, TEXT("TDFluidGpu.WaterAuxOutput"));
			if (!AuxTarget)
			{
				continue;
			}

			if (!ObstacleSdfSRV)
			{
				ObstacleSdfSRV = MakeSRV(GraphBuilder, Context.Textures.ObstacleSdf);
			}
			FTDFluidGpuWaterAuxPackCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuWaterAuxPackCS::FParameters>();
			Parameters->ObstacleSdf = ObstacleSdfSRV;
			Parameters->AuxOutput = MakeUAV(GraphBuilder, AuxTarget);
			Parameters->SliceIndex = static_cast<uint32>(Frame.Parameters[EntryIndex].SliceIndex);
			Parameters->Resolution = static_cast<uint32>(Context.Resolution);
			Parameters->InverseRangeCm = 1.0f / FMath::Clamp(Actor.Settings.ObstacleDistanceOutputRangeCm, 20.0f, 2000.0f);
			TShaderMapRef<FTDFluidGpuWaterAuxPackCS> Shader(Context.ShaderMap);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.WaterAuxPack"), Shader, Parameters, FComputeShaderUtils::GetGroupCount(FIntPoint(Context.Resolution, Context.Resolution), 8));
		}
	}

	void AddWaterBatchGraphPasses(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const FTDFluidGpuFrameInputs& FrameInputs, const FTDFluidGpuFrameResources& FrameResources, FTDFluidGpuBatch& Batch, FTDFluidGpuFrameOutputs& InOutOutputs, FTDFluidGpuPassTimestamps* PassTimestamps)
	{
		const FTDFluidGpuBatchFrame& Frame = Batch.GetFrame();
		RDG_EVENT_SCOPE(GraphBuilder, "TDFluidGpuWaterBatch %dx%d x%d mode %d", Batch.GetKey().ResolutionTier, Batch.GetKey().ResolutionTier, Frame.Entries.Num(), static_cast<int32>(Batch.GetKey().WaterPressureMode));
		FTDFluidGpuWaterGraphTextures WaterTextures;
		const FTDFluidGpuBatchGraphTextures Textures = RegisterWaterBatchStateTextures(GraphBuilder, Batch, WaterTextures);
		FRDGBufferSRVRef ActorParameters = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.ActorParameters"), Frame.Parameters));
		FRDGBufferSRVRef Obstacles = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Obstacles"), Frame.Obstacles));
		WaterTextures.WaterParameters = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.WaterParameters"), Frame.WaterParameters));
		const FTDFluidGpuBatchPassContext Context(GraphBuilder, ShaderMap, FrameInputs, FrameResources, Batch, Textures, ActorParameters, Obstacles, PassTimestamps);

		InOutOutputs.SteppedEntryCount += Context.EntryCount;
		bool bSwapsVelocityStates = false;
		const int32 FinalRippleIndex = AddWaterBatchPasses(Context, WaterTextures, InOutOutputs, bSwapsVelocityStates);
		AddWaterObstacleDistanceAuxPasses(Context);
		if (FinalRippleIndex == 1)
		{
			Swap(Batch.StateTextures.Ripple[0], Batch.StateTextures.Ripple[1]);
		}
		if (bSwapsVelocityStates)
		{
			Swap(Batch.StateTextures.Velocity[0], Batch.StateTextures.Velocity[1]);
		}
	}

	void RecordWaterFrameCsvStats(const TArray<FTDFluidGpuBatch>& Batches)
	{
		int32 RippleSubstepMaximum = 0;
		int32 RippleSubstepCapHitCount = 0;
		int32 PressureModeMaximum = 0;
		for (const FTDFluidGpuBatch& Batch : Batches)
		{
			if (!Batch.GetKey().bUsesWaterLayer || Batch.GetFrame().Entries.IsEmpty())
			{
				continue;
			}

			RippleSubstepMaximum = FMath::Max(RippleSubstepMaximum, Batch.GetFrame().WaterRippleSubstepMaximum);
			RippleSubstepCapHitCount += Batch.GetFrame().WaterRippleSubstepCapHitCount;
			PressureModeMaximum = FMath::Max(PressureModeMaximum, static_cast<int32>(Batch.GetKey().WaterPressureMode));
		}

		CSV_CUSTOM_STAT(TDFluidGpu, WaterRippleSubsteps, RippleSubstepMaximum, ECsvCustomStatOp::Max);
		CSV_CUSTOM_STAT(TDFluidGpu, WaterRippleSubstepCapHits, RippleSubstepCapHitCount, ECsvCustomStatOp::Accumulate);
		CSV_CUSTOM_STAT(TDFluidGpu, WaterPressureMode, PressureModeMaximum, ECsvCustomStatOp::Max);
	}

	FTDFluidGpuMassInputGraph PrepareMassInputGraph(FRDGBuilder& GraphBuilder, const FTDFluidGpuFrameInputs& FrameInputs, FTDFluidGpuBatch& Batch, FTDFluidGpuMassInputRegistry* MassInputRegistry, FTDFluidGpuMassInputSnapshot* MassInputSnapshot)
	{
		FTDFluidGpuMassInputGraph Graph;
		Graph.Snapshot = MassInputSnapshot;
		FTDFluidGpuMassInputBatchState& State = Batch.MassInputState;
		const FTDFluidGpuBatchKey& Key = Batch.GetKey();
		const FTDFluidGpuBatchFrame& Frame = Batch.GetFrame();
		const int32 InputResolution = TDFluidGpuMassInput::ComputeInputResolution(Key.ResolutionTier, Key.MassInputDivisor);
		const int32 Divisor = FMath::Max(1, Key.ResolutionTier / InputResolution);
		const int64 ChannelStride = static_cast<int64>(InputResolution) * InputResolution;
		const int32 SliceCapacity = FMath::Max3(Batch.StateTextures.SliceCapacity, Batch.GetRequiredSliceCapacity(), 1);
		const bool bNeedsAllocation = !State.Accumulation.IsValid() || !State.Counters.IsValid() || State.SliceCapacity < SliceCapacity || State.InputResolution != InputResolution;
		if (bNeedsAllocation)
		{
			State.Accumulation = AllocatePooledBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), static_cast<uint32>(SliceCapacity * ChannelStride * TDFluidGpuSolverConstants::MassInputChannelCount)), TEXT("TDFluidGpu.MassInputAccumulation"));
			State.Counters = AllocatePooledBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), static_cast<uint32>(SliceCapacity * TDFluidGpuSolverConstants::MassInputCounterCount)), TEXT("TDFluidGpu.MassInputCounters"));
			State.SliceCapacity = SliceCapacity;
			State.InputResolution = InputResolution;
			State.SliceOriginPlaneCm.Init(FVector2D::ZeroVector, SliceCapacity);
			State.SliceHasOrigin.Init(false, SliceCapacity);
		}
		Graph.Accumulation = GraphBuilder.RegisterExternalBuffer(State.Accumulation);
		Graph.Counters = GraphBuilder.RegisterExternalBuffer(State.Counters);
		if (bNeedsAllocation)
		{
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Graph.Accumulation, PF_R32_UINT), 0u);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Graph.Counters, PF_R32_UINT), 0u);
		}

		TArray<FTDFluidGpuMassInputEntry> Entries;
		Entries.Reserve(Frame.Entries.Num());
		for (int32 EntryIndex = 0; EntryIndex < Frame.Entries.Num(); ++EntryIndex)
		{
			const FTDFluidGpuSliceEntry& SliceEntry = Frame.Entries[EntryIndex];
			const FTDFluidGpuActorInput& Actor = FrameInputs.Actors[SliceEntry.ActorInputIndex];
			const FTDFluidGpuActorParameters& Parameters = Frame.Parameters[EntryIndex];
			const int32 SliceIndex = SliceEntry.SliceIndex;
			const FVector2D InputCellCm = FVector2D(Parameters.CellSizeCm) * static_cast<double>(Divisor);
			const FVector2D WindowCenter = Actor.WindowCenterPlaneCm.ContainsNaN() ? FVector2D::ZeroVector : Actor.WindowCenterPlaneCm;
			const FVector2D WindowMinimum = WindowCenter - 0.5 * FVector2D(Parameters.ExtentCm);
			const bool bAppliesAccumulation = State.SliceHasOrigin[SliceIndex] && !SliceEntry.bNeedsReset;
			const FVector2D Origin = bAppliesAccumulation ? State.SliceOriginPlaneCm[SliceIndex] : WindowMinimum;
			const int32 RadiusCapCells = FMath::Clamp(Actor.Settings.MassSplatRadiusCapCells, 1, TDFluidGpuSolverConstants::MaximumMassSplatRadiusCapCells);

			ETDFluidGpuMassInputEntryFlags EntryFlags = ETDFluidGpuMassInputEntryFlags::None;
			EntryFlags |= bAppliesAccumulation ? ETDFluidGpuMassInputEntryFlags::AppliesAccumulation : ETDFluidGpuMassInputEntryFlags::None;
			EntryFlags |= Actor.Settings.bMassInputWritesSelectiveDensity ? ETDFluidGpuMassInputEntryFlags::WritesSelectiveDensity : ETDFluidGpuMassInputEntryFlags::None;
			EntryFlags |= Key.bWrapsArea ? ETDFluidGpuMassInputEntryFlags::WrapsArea : ETDFluidGpuMassInputEntryFlags::None;

			FTDFluidGpuMassInputEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.AccumulationBase = static_cast<uint32>(SliceIndex * ChannelStride * TDFluidGpuSolverConstants::MassInputChannelCount);
			Entry.CounterBase = static_cast<uint32>(SliceIndex * TDFluidGpuSolverConstants::MassInputCounterCount);
			Entry.InputResolution = static_cast<uint32>(InputResolution);
			Entry.Flags = static_cast<uint32>(EntryFlags);
			Entry.ShiftOffsetCm = FVector2f(WindowMinimum - Origin);
			Entry.InputCellCm = FVector2f(InputCellCm);
			Entry.SafeCount = TDFluidGpuMassInput::ComputeSafeCount(Parameters.MaxCellSpeedCmPerSec);
			Entry.RadiusCapCells = static_cast<uint32>(RadiusCapCells);
			Graph.CounterBases.Add(Entry.CounterBase);
			Graph.ActorKeys.Add(Actor.ActorKey);

			for (const FTDFluidGpuMassPointInput& MassPoint : Actor.MassPoints)
			{
				if (Graph.Points.Num() >= TDFluidGpuSolverConstants::MaximumMassPointsPerUpload)
				{
					break;
				}
				FTDFluidGpuPackedMassPoint& Packed = Graph.Points.AddDefaulted_GetRef();
				Packed.LocalCm = FVector2f(MassPoint.CenterPlaneCm - Origin);
				Packed.VelocityPlaneCmPerSec = FVector2f(MassPoint.VelocityPlaneCmPerSec);
				Packed.RadiusCm = MassPoint.RadiusCm;
				Packed.DensityTarget = MassPoint.DensityStrength;
				Packed.HeatTarget = MassPoint.HeatStrength;
				Packed.EntryIndexAndFlags = static_cast<uint32>(EntryIndex) | (MassPoint.bWritesVelocity ? 0x80000000u : 0u);
			}

			State.SliceOriginPlaneCm[SliceIndex] = WindowMinimum;
			State.SliceHasOrigin[SliceIndex] = true;
			if (MassInputRegistry)
			{
				FTDFluidGpuMassInputTargetView View;
				View.Accumulation = State.Accumulation;
				View.Counters = State.Counters;
				View.AccumulationBase = Entry.AccumulationBase;
				View.CounterBase = Entry.CounterBase;
				View.InputResolution = InputResolution;
				View.InputCellCm = InputCellCm;
				View.OriginPlaneCm = WindowMinimum;
				View.RadiusCapCells = RadiusCapCells;
				View.bWrapsArea = Key.bWrapsArea;
				View.bIsReady = true;
				MassInputRegistry->UpdateTarget(Actor.ActorKey, View);
			}
		}
		Graph.Entries = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.MassInputEntries"), Entries));
		return Graph;
	}

	void AddBatchPasses(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const FTDFluidGpuFrameInputs& FrameInputs, const FTDFluidGpuFrameResources& FrameResources, FTDFluidGpuBatch& Batch, FTDFluidGpuFrameOutputs& InOutOutputs, FTDFluidGpuPassTimestamps* PassTimestamps, FTDFluidGpuMassInputRegistry* MassInputRegistry = nullptr, FTDFluidGpuMassInputSnapshot* MassInputSnapshot = nullptr)
	{
		const FTDFluidGpuBatchFrame& Frame = Batch.GetFrame();
		if (Frame.Entries.IsEmpty())
		{
			return;
		}

		if (Batch.GetKey().bUsesWaterLayer)
		{
			AddWaterBatchGraphPasses(GraphBuilder, ShaderMap, FrameInputs, FrameResources, Batch, InOutOutputs, PassTimestamps);
			return;
		}

		RDG_EVENT_SCOPE(GraphBuilder, "TDFluidGpuBatch %dx%d x%d", Batch.GetKey().ResolutionTier, Batch.GetKey().ResolutionTier, Frame.Entries.Num());
		const FTDFluidGpuBatchGraphTextures Textures = RegisterBatchStateTextures(GraphBuilder, Batch);
		FRDGBufferSRVRef ActorParameters = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.ActorParameters"), Frame.Parameters));
		FRDGBufferSRVRef Obstacles = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Obstacles"), Frame.Obstacles));
		FTDFluidGpuBatchPassContext Context(GraphBuilder, ShaderMap, FrameInputs, FrameResources, Batch, Textures, ActorParameters, Obstacles, PassTimestamps);
		FTDFluidGpuMassInputGraph MassInputGraph;
		if (Batch.GetKey().MassInputDivisor > 0 && !IsPainterBatch(Batch))
		{
			MassInputGraph = PrepareMassInputGraph(GraphBuilder, FrameInputs, Batch, MassInputRegistry, MassInputSnapshot);
			Context.MassInput = &MassInputGraph;
		}

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
	FTDFluidGpuFrameOutputs AddFramePasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuFrameInputs& FrameInputs, TArray<FTDFluidGpuBatch>& InOutBatches, FTDFluidGpuPassTimestamps* PassTimestamps, FTDFluidGpuMassInputRegistry* MassInputRegistry)
	{
		FTDFluidGpuPreparedFrame PreparedFrame;
		FTDFluidGpuBatch::PrepareFrame(FrameInputs, InOutBatches, PreparedFrame);
		CSV_CUSTOM_STAT(TDFluidGpu, GpuStampOverTickLimit, PreparedFrame.StampBins.OverTickLimitStampCount, ECsvCustomStatOp::Accumulate);
		CSV_CUSTOM_STAT(TDFluidGpu, GpuStampBinOverflow, PreparedFrame.StampBins.BinOverflowEntryCount, ECsvCustomStatOp::Accumulate);
		FTDFluidGpuMassInputSnapshot MassInputSnapshot;
		for (const FTDFluidGpuBatch& Batch : InOutBatches)
		{
			const bool bStepsMassInput = Batch.GetKey().MassInputDivisor > 0 && !Batch.GetKey().bUsesWaterLayer && !IsPainterBatch(Batch);
			MassInputSnapshot.Capacity += bStepsMassInput ? Batch.GetFrame().Entries.Num() : 0;
		}
		if (MassInputSnapshot.Capacity > 0)
		{
			MassInputSnapshot.Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), static_cast<uint32>(MassInputSnapshot.Capacity * TDFluidGpuSolverConstants::MassInputCounterCount)), TEXT("TDFluidGpu.MassInputCounterSnapshot"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MassInputSnapshot.Buffer, PF_R32_UINT), 0u);
		}
		if (MassInputRegistry)
		{
			MassInputRegistry->MarkAllTargetsNotReady();
			for (const uint64 ReleasedActorKey : FrameInputs.ReleasedActorKeys)
			{
				MassInputRegistry->RemoveTarget(ReleasedActorKey);
			}
		}

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

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		FTDFluidGpuFrameResources FrameResources;
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, TDFluidGpuSolver, "TDFluidGpuStep");
			AddPassTimestamp(GraphBuilder, PassTimestamps, TEXT("graph_begin"));
			FrameResources = CreateFrameResources(GraphBuilder, PreparedFrame);
			AddPassTimestamp(GraphBuilder, PassTimestamps, TEXT("frame_resources"));
			for (FTDFluidGpuBatch& Batch : InOutBatches)
			{
				if (!Batch.GetKey().bUsesWaterLayer)
				{
					AddBatchPasses(GraphBuilder, ShaderMap, FrameInputs, FrameResources, Batch, Outputs, PassTimestamps, MassInputRegistry, &MassInputSnapshot);
				}
			}
		}

		if (PreparedFrame.bHasWaterEntries)
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, TDFluidGpuWater, "TDFluidGpuWaterStep");
			RecordWaterFrameCsvStats(InOutBatches);
			for (FTDFluidGpuBatch& Batch : InOutBatches)
			{
				if (Batch.GetKey().bUsesWaterLayer)
				{
					AddBatchPasses(GraphBuilder, ShaderMap, FrameInputs, FrameResources, Batch, Outputs, PassTimestamps);
				}
			}
		}

		Outputs.ReadbackAtlas = FrameResources.ReadbackAtlas;
		Outputs.WaterProbeSamples = FrameResources.WaterProbeSamples;
		Outputs.WaterProbeSlotCount = PreparedFrame.WaterProbeSlotCount;
		if (MassInputSnapshot.Buffer)
		{
			Outputs.MassInputCounterSnapshot = MassInputSnapshot.Buffer;
			Outputs.MassInputCounterActorKeys = MoveTemp(MassInputSnapshot.ActorKeys);
		}
		return Outputs;
	}
}

FTDFluidGpuFrameOutputs AddTDFluidGpuFramePasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuFrameInputs& FrameInputs, TArray<FTDFluidGpuBatch>& InOutBatches, FTDFluidGpuMassInputRegistry* MassInputRegistry)
{
	return AddFramePasses(GraphBuilder, FrameInputs, InOutBatches, nullptr, MassInputRegistry);
}

FTDFluidGpuFrameOutputs AddTDFluidGpuTimestampedFramePasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuFrameInputs& FrameInputs, TArray<FTDFluidGpuBatch>& InOutBatches, FTDFluidGpuPassTimestamps& OutTimestamps, FTDFluidGpuMassInputRegistry* MassInputRegistry)
{
	return AddFramePasses(GraphBuilder, FrameInputs, InOutBatches, &OutTimestamps, MassInputRegistry);
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

void AddTDFluidGpuWaterPressureProbePasses(FRDGBuilder& GraphBuilder, FTDFluidGpuBatch& Batch, int32 EntryIndex, const TArray<int32>& RecordedCycleCounts, FTDFluidGpuWaterPressureProbeReadbacks& OutReadbacks)
{
	const FTDFluidGpuBatchFrame& Frame = Batch.GetFrame();
	const FTDFluidGpuBatchStateTextures& State = Batch.StateTextures;
	const bool bIsHalfPressureWaterBatch = Batch.GetKey().bUsesWaterLayer && IsWaterHalfPressureGraph(Batch);
	const bool bHasWaterStateTextures = State.Velocity[1].IsValid() && State.ObstacleSdf.IsValid() && State.WaterGuideField.IsValid() && State.WaterSurfaceHeightField.IsValid() && State.WaterTerrainField.IsValid();
	const bool bHasEntryParameters = Frame.Parameters.IsValidIndex(EntryIndex) && Frame.WaterParameters.Num() == Frame.Parameters.Num() && Frame.Entries.Num() == Frame.Parameters.Num();
	if (!bIsHalfPressureWaterBatch || !bHasWaterStateTextures || !bHasEntryParameters)
	{
		return;
	}

	const uint32 SliceIndex = static_cast<uint32>(Frame.Parameters[EntryIndex].SliceIndex);
	FTDFluidGpuBatchGraphTextures RawTextures;
	RawTextures.Velocity[1] = GraphBuilder.RegisterExternalTexture(State.Velocity[1], TEXT("TDFluidGpu.WaterProbeVelocity"));
	RawTextures.ObstacleSdf = GraphBuilder.RegisterExternalTexture(State.ObstacleSdf, TEXT("TDFluidGpu.WaterProbeObstacleSdf"));
	RawTextures.PressureSolution[0] = GraphBuilder.CreateTexture(MakeStateArrayDesc(ComputeWaterGraphPressureResolution(Batch), State.SliceCapacity, PressureSolutionFormat), TEXT("TDFluidGpu.WaterProbeHalfPressure"));
	AddClearUAVPass(GraphBuilder, MakeUAV(GraphBuilder, RawTextures.PressureSolution[0]), 0.0f);

	FTDFluidGpuWaterGraphTextures WaterTextures;
	WaterTextures.GuideField = GraphBuilder.RegisterExternalTexture(State.WaterGuideField, TEXT("TDFluidGpu.WaterProbeGuideField"));
	WaterTextures.SurfaceHeightField = GraphBuilder.RegisterExternalTexture(State.WaterSurfaceHeightField, TEXT("TDFluidGpu.WaterProbeSurfaceHeightField"));
	WaterTextures.TerrainField = GraphBuilder.RegisterExternalTexture(State.WaterTerrainField, TEXT("TDFluidGpu.WaterProbeTerrainField"));
	WaterTextures.WaterParameters = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.WaterProbeWaterParameters"), Frame.WaterParameters));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	FRDGBufferSRVRef ActorParameters = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.WaterProbeActorParameters"), Frame.Parameters));
	const FTDFluidGpuFrameInputs ProbeFrameInputs;
	const FTDFluidGpuFrameResources ProbeFrameResources;
	const FTDFluidGpuBatchPassContext RawContext(GraphBuilder, ShaderMap, ProbeFrameInputs, ProbeFrameResources, Batch, RawTextures, ActorParameters, nullptr, nullptr);
	AddWaterComposePasses(RawContext, WaterTextures);

	FTDFluidGpuBatchGraphTextures SolverTextures = RawTextures;
	SolverTextures.ObstacleSdf = WaterTextures.CompositeSdf;
	const FTDFluidGpuBatchPassContext SolverContext(GraphBuilder, ShaderMap, ProbeFrameInputs, ProbeFrameResources, Batch, SolverTextures, ActorParameters, nullptr, nullptr);
	const FTDFluidGpuMultigridPassContext MultigridContext = MakeWaterMultigridPassContext(SolverContext);
	const FTDFluidGpuMultigridLevels Levels = CreateMultigridLevels(MultigridContext);

	OutReadbacks.SliceIndex = SliceIndex;
	OutReadbacks.Velocity = MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.WaterProbeVelocityReadback"));
	OutReadbacks.CompositeSdf = MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.WaterProbeCompositeSdfReadback"));
	OutReadbacks.SurfaceHeightField = MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.WaterProbeSurfaceHeightFieldReadback"));
	OutReadbacks.OpenBorderFlags = MakeUnique<FRHIGPUBufferReadback>(TEXT("TDFluidGpu.WaterProbeOpenBorderFlagsReadback"));
	AddEnqueueCopyPass(GraphBuilder, OutReadbacks.Velocity.Get(), RawTextures.Velocity[1], FResolveRect(), SliceIndex);
	AddEnqueueCopyPass(GraphBuilder, OutReadbacks.CompositeSdf.Get(), WaterTextures.CompositeSdf, FResolveRect(), SliceIndex);
	AddEnqueueCopyPass(GraphBuilder, OutReadbacks.SurfaceHeightField.Get(), WaterTextures.SurfaceHeightField, FResolveRect(), SliceIndex);
	AddEnqueueCopyPass(GraphBuilder, OutReadbacks.OpenBorderFlags.Get(), WaterTextures.OpenBorderFlags, static_cast<uint32>(Frame.Entries.Num() * sizeof(uint32)));

	int32 LastCycleCount = 0;
	for (const int32 RecordedCycleCount : RecordedCycleCounts)
	{
		LastCycleCount = FMath::Max(LastCycleCount, RecordedCycleCount);
	}

	for (int32 CycleCount = 1; CycleCount <= LastCycleCount; ++CycleCount)
	{
		AddWaterHalfPressureCyclePasses(SolverContext, WaterTextures, MultigridContext, Levels, RawTextures.Velocity[1], RawTextures.PressureSolution[0], 1.0f, CycleCount == 1);
		if (!RecordedCycleCounts.Contains(CycleCount))
		{
			continue;
		}

		FRDGTextureRef PressureFull = AddWaterUpsamplePressurePass(SolverContext, WaterTextures, Levels, RawTextures.PressureSolution[0]);
		TUniquePtr<FRHIGPUTextureReadback>& Readback = OutReadbacks.PressureFullByCycle.Add_GetRef(MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.WaterProbePressureFullReadback")));
		AddEnqueueCopyPass(GraphBuilder, Readback.Get(), PressureFull, FResolveRect(), SliceIndex);
		OutReadbacks.RecordedCycleCounts.Add(CycleCount);
	}
}

int32 ComputeTDFluidGpuWaterHalfPostSmoothIterations()
{
	return FMath::Clamp(CVarTDFluidWaterGpuHalfPostSmoothIterations.GetValueOnAnyThread(), 0, MaximumWaterHalfPostSmoothIterations);
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
		TotalBytes += ComputePooledTextureBytes(State.Ripple[PingPongIndex]);
	}
	TotalBytes += ComputePooledTextureBytes(State.ObstacleSdf);
	TotalBytes += ComputePooledTextureBytes(State.TerrainElevation);
	TotalBytes += ComputePooledTextureBytes(State.PainterPrimaryDensity);
	TotalBytes += ComputePooledTextureBytes(State.PainterSelectiveDensity);
	TotalBytes += ComputePooledTextureBytes(State.WaterGuideField);
	TotalBytes += ComputePooledTextureBytes(State.WaterSurfaceHeightField);
	TotalBytes += ComputePooledTextureBytes(State.WaterTerrainField);
	TotalBytes += ComputePooledTextureBytes(State.WaterWetness);
	return TotalBytes;
}

namespace
{
	void CopyWaterProbeSamples(FRHIGPUBufferReadback* Readback, int32 SlotCount, TArray<FTDFluidGpuWaterProbeSample>& OutSamples)
	{
		OutSamples.Reset();
		if (!Readback || SlotCount <= 0)
		{
			return;
		}

		const FTDFluidGpuWaterProbeSample* Samples = static_cast<const FTDFluidGpuWaterProbeSample*>(Readback->Lock(WaterProbeReadbackBytes));
		if (!Samples)
		{
			return;
		}

		OutSamples.Append(Samples, FMath::Min(SlotCount, TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame));
		Readback->Unlock();
	}

	void CopyMassInputCounters(FRHIGPUBufferReadback* Readback, TConstArrayView<uint64> ActorKeys, uint32 FrameNumber, TArray<FTDFluidGpuMassInputCounters>& OutCounters)
	{
		OutCounters.Reset();
		if (!Readback || ActorKeys.IsEmpty())
		{
			return;
		}

		const uint32 ByteCount = static_cast<uint32>(ActorKeys.Num() * TDFluidGpuSolverConstants::MassInputCounterCount * sizeof(uint32));
		const uint32* Counters = static_cast<const uint32*>(Readback->Lock(ByteCount));
		if (!Counters)
		{
			return;
		}

		for (int32 Index = 0; Index < ActorKeys.Num(); ++Index)
		{
			const uint32* EntryCounters = Counters + Index * TDFluidGpuSolverConstants::MassInputCounterCount;
			FTDFluidGpuMassInputCounters& Entry = OutCounters.AddDefaulted_GetRef();
			Entry.ActorKey = ActorKeys[Index];
			Entry.SplatCount = EntryCounters[0];
			Entry.OutsideCount = EntryCounters[1];
			Entry.bSaturated = EntryCounters[2] != 0;
			Entry.FrameNumber = FrameNumber;
		}
		Readback->Unlock();
	}
}

FTDFluidGpuReadbackRing::FTDFluidGpuReadbackRing()
{
	for (FTDFluidGpuReadbackSlot& Slot : Slots)
	{
		Slot.Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.ReadbackAtlas"));
		Slot.WaterProbeReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("TDFluidGpu.WaterProbeReadback"));
		Slot.MassInputCounterReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("TDFluidGpu.MassInputCounterReadback"));
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

	if (FreeSlot->Layout.GetAtlasSize() != FrameOutputs.ReadbackLayout.GetAtlasSize())
	{
		FreeSlot->Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("TDFluidGpu.ReadbackAtlas"));
	}

	AddEnqueueCopyPass(GraphBuilder, FreeSlot->Readback.Get(), FrameOutputs.ReadbackAtlas);
	FreeSlot->WaterProbeSlotCount = 0;
	if (FrameOutputs.WaterProbeSamples && FrameOutputs.WaterProbeSlotCount > 0)
	{
		AddEnqueueCopyPass(GraphBuilder, FreeSlot->WaterProbeReadback.Get(), FrameOutputs.WaterProbeSamples, WaterProbeReadbackBytes);
		FreeSlot->WaterProbeSlotCount = FMath::Min(FrameOutputs.WaterProbeSlotCount, TDFluidGpuSolverConstants::MaximumWaterProbesPerFrame);
	}
	FreeSlot->MassInputCounterActorKeys.Reset();
	if (FrameOutputs.MassInputCounterSnapshot && !FrameOutputs.MassInputCounterActorKeys.IsEmpty())
	{
		AddEnqueueCopyPass(GraphBuilder, FreeSlot->MassInputCounterReadback.Get(), FrameOutputs.MassInputCounterSnapshot, static_cast<uint32>(FrameOutputs.MassInputCounterActorKeys.Num() * TDFluidGpuSolverConstants::MassInputCounterCount * sizeof(uint32)));
		FreeSlot->MassInputCounterActorKeys = FrameOutputs.MassInputCounterActorKeys;
	}
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
		const bool bIsWaterProbeReady = Slot.WaterProbeSlotCount == 0 || Slot.WaterProbeReadback->IsReady();
		const bool bIsMassInputReady = Slot.MassInputCounterActorKeys.IsEmpty() || Slot.MassInputCounterReadback->IsReady();
		const bool bIsNewerReadySlot = Slot.bIsPending && Slot.Readback->IsReady() && bIsWaterProbeReady && bIsMassInputReady && (!LatestReadySlot || Slot.EnqueueOrder > LatestReadySlot->EnqueueOrder);
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
	CopyWaterProbeSamples(LatestReadySlot->WaterProbeReadback.Get(), LatestReadySlot->WaterProbeSlotCount, OutFrame.WaterProbeSamples);
	CopyMassInputCounters(LatestReadySlot->MassInputCounterReadback.Get(), LatestReadySlot->MassInputCounterActorKeys, LatestReadySlot->FrameNumber, OutFrame.MassInputCounters);

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
