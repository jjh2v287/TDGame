#include "TDFluidGpuFog3D.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "PooledRenderTarget.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "TDFluidGpuFog3DPasses.h"
#include "TextureResource.h"

DECLARE_GPU_STAT(TDFog3DGroundCaptureConvert);
CSV_DEFINE_CATEGORY_MODULE(TDFLUIDGPU_API, TDFog3D, true);

class FTDFluidGpuFog3DShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FTDFluidGpuFog3DShader() {}
	FTDFluidGpuFog3DShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};

class FTDFluidGpuFog3DBakeBlockedCS : public FTDFluidGpuFog3DShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DBakeBlockedCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DBakeBlockedCS, FTDFluidGpuFog3DShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuFog3DCommonParameters, Common)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuFog3DObstacle>, Obstacles)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, ColumnTerrainIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, CellBlockedOut)
		SHADER_PARAMETER(uint32, ObstacleCount)
		SHADER_PARAMETER(uint32, GroundFollow)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DBakeBlockedCS, "/TDFluidGpu/Private/TDFluidGpuFog3D.usf", "Fog3DBakeBlockedCS", SF_Compute);

class FTDFluidGpuFog3DUploadGroundCS : public FTDFluidGpuFog3DShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DUploadGroundCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DUploadGroundCS, FTDFluidGpuFog3DShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, GroundUploadIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, GroundHeightOut)
		SHADER_PARAMETER(uint32, GroundUploadSize)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DUploadGroundCS, "/TDFluidGpu/Private/TDFluidGpuFog3D.usf", "Fog3DUploadGroundCS", SF_Compute);

class FTDFluidGpuFog3DConvertGroundCS : public FTDFluidGpuFog3DShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DConvertGroundCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DConvertGroundCS, FTDFluidGpuFog3DShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, GroundCaptureDepthIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, GroundHeightOut)
		SHADER_PARAMETER(uint32, GroundCaptureSize)
		SHADER_PARAMETER(float, GroundDecodeOffsetCm)
		SHADER_PARAMETER(float, GroundMissingHeightCm)
		SHADER_PARAMETER(float, GroundMissingBelowFloorCm)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DConvertGroundCS, "/TDFluidGpu/Private/TDFluidGpuFog3D.usf", "Fog3DConvertGroundCS", SF_Compute);

class FTDFluidGpuFog3DBakeTerrainCS : public FTDFluidGpuFog3DShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DBakeTerrainCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DBakeTerrainCS, FTDFluidGpuFog3DShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuFog3DCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, GroundHeightIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ColumnTerrainOut)
		SHADER_PARAMETER(FIntPoint, GroundTexelOffset)
		SHADER_PARAMETER(uint32, GroundFollow)
		SHADER_PARAMETER(float, GroundWallStepCm)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DBakeTerrainCS, "/TDFluidGpu/Private/TDFluidGpuFog3D.usf", "Fog3DBakeTerrainCS", SF_Compute);

class FTDFluidGpuFog3DBakeFaceFlagsCS : public FTDFluidGpuFog3DShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DBakeFaceFlagsCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DBakeFaceFlagsCS, FTDFluidGpuFog3DShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuFog3DCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, CellBlockedIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, FaceFlagsOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DBakeFaceFlagsCS, "/TDFluidGpu/Private/TDFluidGpuFog3D.usf", "Fog3DBakeFaceFlagsCS", SF_Compute);

class FTDFluidGpuFog3DCleanCS : public FTDFluidGpuFog3DShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DCleanCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DCleanCS, FTDFluidGpuFog3DShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuFog3DCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, WindIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, FaceFlagsIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, WindOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, DisplacementFillOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, PressureOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DCleanCS, "/TDFluidGpu/Private/TDFluidGpuFog3D.usf", "Fog3DCleanCS", SF_Compute);

class FTDFluidGpuFog3DOverrideCS : public FTDFluidGpuFog3DShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DOverrideCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DOverrideCS, FTDFluidGpuFog3DShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuFog3DCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, WindIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, DisplacementFillIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, FaceFlagsIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, WindOverride)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, FillOperation)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, WindOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, DisplacementFillOut)
		SHADER_PARAMETER(uint32, HasWindOverride)
		SHADER_PARAMETER(uint32, HasFillOperation)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DOverrideCS, "/TDFluidGpu/Private/TDFluidGpuFog3D.usf", "Fog3DOverrideCS", SF_Compute);

class FTDFluidGpuFog3DAdvectCS : public FTDFluidGpuFog3DShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DAdvectCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DAdvectCS, FTDFluidGpuFog3DShader);

	class FInputOcclusionDimension : SHADER_PERMUTATION_BOOL("TDFOG3D_INPUT_OCCLUSION");
	using FPermutationDomain = TShaderPermutationDomain<FInputOcclusionDimension>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuFog3DCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, WindIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, DisplacementFillIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, FaceFlagsIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuFog3DSphere>, Spheres)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuFog3DSphereFrame>, SphereFrames)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, ColumnTerrainIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, WindOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, DisplacementFillOut)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutputVolume)
		SHADER_PARAMETER(uint32, SphereCount)
		SHADER_PARAMETER(uint32, GroundFollow)
		SHADER_PARAMETER(float, GroundWallStepCm)
		SHADER_PARAMETER(int32, Mode)
		SHADER_PARAMETER(float, DeltaSeconds)
		SHADER_PARAMETER(float, WindRefillFactor)
		SHADER_PARAMETER(float, DisplacementRefillFactorHorizontal)
		SHADER_PARAMETER(float, DisplacementRefillFactorVertical)
		SHADER_PARAMETER(float, FillRefillFactor)
		SHADER_PARAMETER(int32, EdgeBandCells)
		SHADER_PARAMETER_ARRAY(FVector4f, EdgeBandFactors, [4])
		SHADER_PARAMETER(FVector3f, MoveLimitCm)
		SHADER_PARAMETER(uint32, ClampedTimeWindBlend)
		SHADER_PARAMETER(float, WindBlendStepRepeat)
		SHADER_PARAMETER(uint32, WindBlendIsUnitStep)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InputReachIn)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DAdvectCS, "/TDFluidGpu/Private/TDFluidGpuFog3D.usf", "Fog3DAdvectCS", SF_Compute);

class FTDFluidGpuFog3DInputFrameCS : public FTDFluidGpuFog3DShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DInputFrameCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DInputFrameCS, FTDFluidGpuFog3DShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuFog3DCommonParameters, Common)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuFog3DSphere>, Spheres)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, ColumnTerrainIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FTDFluidGpuFog3DSphereFrame>, SphereFramesOut)
		SHADER_PARAMETER(uint32, SphereCount)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DInputFrameCS, "/TDFluidGpu/Private/TDFluidGpuFog3D.usf", "Fog3DInputFrameCS", SF_Compute);

class FTDFluidGpuFog3DInputReachCS : public FTDFluidGpuFog3DShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DInputReachCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DInputReachCS, FTDFluidGpuFog3DShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuFog3DCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, FaceFlagsIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, CellBlockedIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, ColumnTerrainIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuFog3DSphere>, Spheres)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTDFluidGpuFog3DSphereAnchor>, SphereAnchors)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, InputReachOut)
		SHADER_PARAMETER(uint32, GroundFollow)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DInputReachCS, "/TDFluidGpu/Private/TDFluidGpuFog3DInputReach.usf", "Fog3DInputReachCS", SF_Compute);

class FTDFluidGpuFog3DDivergenceCS : public FTDFluidGpuFog3DShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DDivergenceCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DDivergenceCS, FTDFluidGpuFog3DShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuFog3DCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, WindIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, DivergenceOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DDivergenceCS, "/TDFluidGpu/Private/TDFluidGpuFog3D.usf", "Fog3DDivergenceCS", SF_Compute);

class FTDFluidGpuFog3DGradientCS : public FTDFluidGpuFog3DShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DGradientCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DGradientCS, FTDFluidGpuFog3DShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuFog3DCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, WindIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, PressureIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<uint>, FaceFlagsIn)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, WindOut)
		SHADER_PARAMETER(uint32, AppliesPressureGradient)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DGradientCS, "/TDFluidGpu/Private/TDFluidGpuFog3D.usf", "Fog3DGradientCS", SF_Compute);

class FTDFluidGpuFog3DDumpCS : public FTDFluidGpuFog3DShader
{
	DECLARE_GLOBAL_SHADER(FTDFluidGpuFog3DDumpCS);
	SHADER_USE_PARAMETER_STRUCT(FTDFluidGpuFog3DDumpCS, FTDFluidGpuFog3DShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTDFluidGpuFog3DCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, WindIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float4>, DisplacementFillIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, PressureIn)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, DivergenceIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, DumpOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTDFluidGpuFog3DDumpCS, "/TDFluidGpu/Private/TDFluidGpuFog3D.usf", "Fog3DDumpCS", SF_Compute);

namespace TDFluidGpuFog3DPrivate
{
	constexpr int32 Fog3DGroupSizeX = 8;
	constexpr int32 Fog3DGroupSizeY = 8;
	constexpr int32 Fog3DGroupSizeZ = 4;
	constexpr int32 Fog3DMinimumResolution = 16;
	constexpr int32 Fog3DWindowAlignmentCells = 16;
	constexpr int32 Fog3DEdgeBandFactorVectorCount = 4;
	constexpr int32 Fog3DInputFrameGroupSize = 64;
	constexpr int64 Fog3DStateBytesPerCell = 74;
	constexpr int64 Fog3DDumpBytesPerValue = 4;
	constexpr int64 Fog3DColumnTerrainBytesPerTexel = 16;
	constexpr int64 Fog3DGroundHeightBytesPerTexel = 4;

	struct FTDFluidGpuFog3DGraphTextures
	{
		FRDGTextureRef Wind[2] = {};
		FRDGTextureRef DisplacementFill[2] = {};
		FRDGTextureRef Pressure = nullptr;
		FRDGTextureRef Divergence = nullptr;
		FRDGTextureRef CellBlocked = nullptr;
		FRDGTextureRef FaceFlags = nullptr;
		FRDGTextureRef ColumnTerrain = nullptr;
	};

	int64 GetFog3DCellCount(int32 Resolution)
	{
		return static_cast<int64>(Resolution) * Resolution * TDFluidGpuFog3DLayerCount;
	}

	FIntVector GetFog3DGridSize(int32 Resolution)
	{
		return FIntVector(Resolution, Resolution, TDFluidGpuFog3DLayerCount);
	}

	FIntVector GetFog3DGroupCount(int32 Resolution)
	{
		return FComputeShaderUtils::GetGroupCount(GetFog3DGridSize(Resolution), FIntVector(Fog3DGroupSizeX, Fog3DGroupSizeY, Fog3DGroupSizeZ));
	}

	bool IsFog3DOverrideArrayLengthValid(int32 Length, int32 Resolution)
	{
		return Length == 0 || Length == GetFog3DCellCount(Resolution);
	}

	bool IsFog3DOutputTextureValid(const FRHITexture* Texture, int32 Resolution)
	{
		if (!Texture)
		{
			return false;
		}

		const FRHITextureDesc& Desc = Texture->GetDesc();
		return Desc.Dimension == ETextureDimension::Texture3D
			&& Desc.Format == PF_FloatRGBA
			&& EnumHasAnyFlags(Desc.Flags, TexCreate_UAV)
			&& Desc.Extent == FIntPoint(Resolution, Resolution)
			&& Desc.Depth == TDFluidGpuFog3DLayerCount;
	}

	FRHITexture* FindFog3DOutputTexture(const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		return FrameInput.OutputTarget ? FrameInput.OutputTarget->GetRenderTargetTexture().GetReference() : nullptr;
	}

	bool IsFog3DFrameInputValid(const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		const int32 Resolution = FrameInput.Resolution;
		if (Resolution < Fog3DMinimumResolution || !FMath::IsPowerOfTwo(Resolution))
		{
			return false;
		}

		if (FrameInput.WindowMinCell.X % Fog3DWindowAlignmentCells != 0 || FrameInput.WindowMinCell.Y % Fog3DWindowAlignmentCells != 0)
		{
			return false;
		}

		if (!(FrameInput.CellSizeCm > 0.0f) || !(FrameInput.LayerHeightCm > 0.0f) || !FMath::IsFinite(FrameInput.CellSizeCm) || !FMath::IsFinite(FrameInput.LayerHeightCm))
		{
			return false;
		}

		if (FrameInput.Mode != 1 && FrameInput.Mode != 2)
		{
			return false;
		}

		if (!FMath::IsFinite(FrameInput.DeltaSeconds) || !(FrameInput.DeltaSeconds > 0.0f))
		{
			return false;
		}

		if (FrameInput.Spheres.Num() > TDFluidGpuFog3DMaximumSphereCount || FrameInput.Obstacles.Num() > TDFluidGpuFog3DMaximumObstacleCount)
		{
			return false;
		}

		if (FrameInput.bOccludesInputBehindObstacles && FrameInput.SphereAnchors.Num() != FrameInput.Spheres.Num())
		{
			return false;
		}

		if (FrameInput.Tuning.EdgeBandCells < 0 || FrameInput.Tuning.EdgeBandCells > TDFluidGpuFog3DMaximumEdgeBandCells || FrameInput.Tuning.PressureSweepCount < 0)
		{
			return false;
		}

		const FTDFluidGpuFog3DStateOverride* StateOverride = FrameInput.StateOverride;
		if (StateOverride && (!IsFog3DOverrideArrayLengthValid(StateOverride->WindFaces.Num(), Resolution) || !IsFog3DOverrideArrayLengthValid(StateOverride->FillOperations.Num(), Resolution)))
		{
			return false;
		}

		return !FrameInput.OutputTarget || IsFog3DOutputTextureValid(FindFog3DOutputTexture(FrameInput), Resolution);
	}

	bool IsFog3DGroundUploading(const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		return FrameInput.bFollowsGround && FrameInput.bGroundChanged && FrameInput.GroundUpload;
	}

	bool DoesFog3DGroundCoverWindow(const FIntPoint& GroundMinCell, int32 GroundSize, const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		const int64 RingMinX = static_cast<int64>(FrameInput.WindowMinCell.X) - 1;
		const int64 RingMinY = static_cast<int64>(FrameInput.WindowMinCell.Y) - 1;
		const int64 RingEndX = static_cast<int64>(FrameInput.WindowMinCell.X) + FrameInput.Resolution + 1;
		const int64 RingEndY = static_cast<int64>(FrameInput.WindowMinCell.Y) + FrameInput.Resolution + 1;
		return GroundMinCell.X <= RingMinX && GroundMinCell.Y <= RingMinY
			&& static_cast<int64>(GroundMinCell.X) + GroundSize >= RingEndX
			&& static_cast<int64>(GroundMinCell.Y) + GroundSize >= RingEndY;
	}

	bool IsFog3DGroundUploadValid(const FTDFluidGpuFog3DGroundUpload& GroundUpload)
	{
		const FIntPoint Size = GroundUpload.Size;
		if (Size.X <= 0 || Size.X != Size.Y || Size.X > static_cast<int32>(GMaxTextureDimensions))
		{
			return false;
		}

		if (GroundUpload.HeightsCm.Num() != static_cast<int64>(Size.X) * Size.Y)
		{
			return false;
		}

		for (const float HeightCm : GroundUpload.HeightsCm)
		{
			if (!FMath::IsFinite(HeightCm))
			{
				return false;
			}
		}
		return true;
	}

	bool IsFog3DGroundCapturing(const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		return FrameInput.bFollowsGround && FrameInput.bGroundChanged && FrameInput.GroundCapture.DepthTarget;
	}

	bool IsFog3DGroundPlaneTargetValid(FTextureRenderTargetResource* Target, int32 Size)
	{
		const FRHITexture* Texture = Target ? Target->GetRenderTargetTexture().GetReference() : nullptr;
		if (!Texture)
		{
			return false;
		}

		const FRHITextureDesc& Desc = Texture->GetDesc();
		return Desc.Dimension == ETextureDimension::Texture2D && Desc.Format == PF_R32_FLOAT && Desc.Extent == FIntPoint(Size, Size);
	}

	bool IsFog3DGroundCaptureValid(const FTDFluidGpuFog3DGroundCapture& GroundCapture, FTextureRenderTargetResource* MaterialTarget)
	{
		if (GroundCapture.Size <= 0 || GroundCapture.Size > static_cast<int32>(GMaxTextureDimensions))
		{
			return false;
		}

		const bool bHasFiniteDecode = FMath::IsFinite(GroundCapture.DecodeOffsetCm) && FMath::IsFinite(GroundCapture.MissingHeightCm);
		if (!bHasFiniteDecode || !FMath::IsFinite(GroundCapture.MissingBelowFloorCm) || !(GroundCapture.MissingBelowFloorCm > 0.0f))
		{
			return false;
		}

		if (!IsFog3DGroundPlaneTargetValid(GroundCapture.DepthTarget, GroundCapture.Size))
		{
			return false;
		}
		return !MaterialTarget || IsFog3DGroundPlaneTargetValid(MaterialTarget, GroundCapture.Size);
	}

	bool IsFog3DGroundInputValid(const FTDFluidGpuFog3DState& State, const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		if (!FrameInput.bFollowsGround)
		{
			return true;
		}

		if (!FMath::IsFinite(FrameInput.GroundWallStepCm) || !(FrameInput.GroundWallStepCm > 0.0f))
		{
			return false;
		}

		if (FrameInput.GroundUpload && FrameInput.GroundCapture.DepthTarget)
		{
			return false;
		}

		if (IsFog3DGroundUploading(FrameInput))
		{
			const FTDFluidGpuFog3DGroundUpload& GroundUpload = *FrameInput.GroundUpload;
			return IsFog3DGroundUploadValid(GroundUpload) && DoesFog3DGroundCoverWindow(GroundUpload.MinCell, GroundUpload.Size.X, FrameInput);
		}

		if (IsFog3DGroundCapturing(FrameInput))
		{
			const FTDFluidGpuFog3DGroundCapture& GroundCapture = FrameInput.GroundCapture;
			return IsFog3DGroundCaptureValid(GroundCapture, FrameInput.GroundMaterialTarget) && DoesFog3DGroundCoverWindow(GroundCapture.MinCell, GroundCapture.Size, FrameInput);
		}

		return State.bHasGround && State.GroundHeightTarget.IsValid() && DoesFog3DGroundCoverWindow(State.GroundMinCell, State.GroundSize, FrameInput);
	}

	bool IsFog3DWindowJumpTooFar(const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		const int64 DeltaX = static_cast<int64>(FrameInput.WindowMinCell.X) - FrameInput.PreviousWindowMinCell.X;
		const int64 DeltaY = static_cast<int64>(FrameInput.WindowMinCell.Y) - FrameInput.PreviousWindowMinCell.Y;
		return FMath::Abs(DeltaX) >= FrameInput.Resolution || FMath::Abs(DeltaY) >= FrameInput.Resolution;
	}

	bool HasFog3DStateTextures(const FTDFluidGpuFog3DState& State, int32 Resolution)
	{
		return State.Resolution == Resolution
			&& State.WindTargets[0].IsValid() && State.WindTargets[1].IsValid()
			&& State.DisplacementFillTargets[0].IsValid() && State.DisplacementFillTargets[1].IsValid()
			&& State.PressureTarget.IsValid() && State.DivergenceTarget.IsValid()
			&& State.CellBlockedTarget.IsValid() && State.FaceFlagsTarget.IsValid();
	}

	bool IsFog3DPreviousWindowStale(const FTDFluidGpuFog3DState& State, const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		if (FrameInput.bResetsState || !State.bHasStepped || !HasFog3DStateTextures(State, FrameInput.Resolution))
		{
			return false;
		}
		return FrameInput.PreviousWindowMinCell != State.WindowMinCell;
	}

	FRDGTextureRef CreateFog3DTexture(FRDGBuilder& GraphBuilder, int32 Resolution, EPixelFormat Format, const TCHAR* Name)
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create3D(GetFog3DGridSize(Resolution), Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
		return GraphBuilder.CreateTexture(Desc, Name);
	}

	FRDGTextureRef CreateFog3DPlaneTexture(FRDGBuilder& GraphBuilder, int32 Size, EPixelFormat Format, const TCHAR* Name)
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(Size, Size), Format, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
		return GraphBuilder.CreateTexture(Desc, Name);
	}

	FTDFluidGpuFog3DGraphTextures CreateFog3DStateTextures(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, int32 Resolution)
	{
		FTDFluidGpuFog3DGraphTextures Textures;
		Textures.Wind[0] = CreateFog3DTexture(GraphBuilder, Resolution, PF_A32B32G32R32F, TEXT("TDFluidGpu.Fog3DWindA"));
		Textures.Wind[1] = CreateFog3DTexture(GraphBuilder, Resolution, PF_A32B32G32R32F, TEXT("TDFluidGpu.Fog3DWindB"));
		Textures.DisplacementFill[0] = CreateFog3DTexture(GraphBuilder, Resolution, PF_A32B32G32R32F, TEXT("TDFluidGpu.Fog3DDisplacementFillA"));
		Textures.DisplacementFill[1] = CreateFog3DTexture(GraphBuilder, Resolution, PF_A32B32G32R32F, TEXT("TDFluidGpu.Fog3DDisplacementFillB"));
		Textures.Pressure = CreateFog3DTexture(GraphBuilder, Resolution, PF_R32_FLOAT, TEXT("TDFluidGpu.Fog3DPressure"));
		Textures.Divergence = CreateFog3DTexture(GraphBuilder, Resolution, PF_R32_FLOAT, TEXT("TDFluidGpu.Fog3DDivergence"));
		Textures.CellBlocked = CreateFog3DTexture(GraphBuilder, Resolution, PF_R8_UINT, TEXT("TDFluidGpu.Fog3DCellBlocked"));
		Textures.FaceFlags = CreateFog3DTexture(GraphBuilder, Resolution, PF_R8_UINT, TEXT("TDFluidGpu.Fog3DFaceFlags"));

		State.WindTargets[0] = GraphBuilder.ConvertToExternalTexture(Textures.Wind[0]);
		State.WindTargets[1] = GraphBuilder.ConvertToExternalTexture(Textures.Wind[1]);
		State.DisplacementFillTargets[0] = GraphBuilder.ConvertToExternalTexture(Textures.DisplacementFill[0]);
		State.DisplacementFillTargets[1] = GraphBuilder.ConvertToExternalTexture(Textures.DisplacementFill[1]);
		State.PressureTarget = GraphBuilder.ConvertToExternalTexture(Textures.Pressure);
		State.DivergenceTarget = GraphBuilder.ConvertToExternalTexture(Textures.Divergence);
		State.CellBlockedTarget = GraphBuilder.ConvertToExternalTexture(Textures.CellBlocked);
		State.FaceFlagsTarget = GraphBuilder.ConvertToExternalTexture(Textures.FaceFlags);
		State.Resolution = Resolution;
		State.bHasStepped = false;
		return Textures;
	}

	FTDFluidGpuFog3DGraphTextures RegisterFog3DStateTextures(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DState& State)
	{
		FTDFluidGpuFog3DGraphTextures Textures;
		Textures.Wind[0] = GraphBuilder.RegisterExternalTexture(State.WindTargets[0]);
		Textures.Wind[1] = GraphBuilder.RegisterExternalTexture(State.WindTargets[1]);
		Textures.DisplacementFill[0] = GraphBuilder.RegisterExternalTexture(State.DisplacementFillTargets[0]);
		Textures.DisplacementFill[1] = GraphBuilder.RegisterExternalTexture(State.DisplacementFillTargets[1]);
		Textures.Pressure = GraphBuilder.RegisterExternalTexture(State.PressureTarget);
		Textures.Divergence = GraphBuilder.RegisterExternalTexture(State.DivergenceTarget);
		Textures.CellBlocked = GraphBuilder.RegisterExternalTexture(State.CellBlockedTarget);
		Textures.FaceFlags = GraphBuilder.RegisterExternalTexture(State.FaceFlagsTarget);
		return Textures;
	}

	void ReleaseFog3DGroundTextures(FTDFluidGpuFog3DState& State)
	{
		State.ColumnTerrainTarget.SafeRelease();
		State.GroundHeightTarget.SafeRelease();
		State.GroundMinCell = FIntPoint::ZeroValue;
		State.GroundSize = 0;
		State.bHasGround = false;
	}

	bool HasFog3DColumnTerrain(const FTDFluidGpuFog3DState& State, int32 Resolution)
	{
		return State.ColumnTerrainTarget.IsValid() && State.ColumnTerrainTarget->GetDesc().Extent == FIntPoint(Resolution + 2, Resolution + 2);
	}

	FRDGTextureRef CreateFog3DEmptyColumnTerrainTexture(FRDGBuilder& GraphBuilder)
	{
		FRDGTextureRef EmptyColumnTerrain = CreateFog3DPlaneTexture(GraphBuilder, 1, PF_A32B32G32R32F, TEXT("TDFluidGpu.Fog3DEmptyColumnTerrain"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EmptyColumnTerrain), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		return EmptyColumnTerrain;
	}

	FRDGTextureRef PrepareFog3DColumnTerrain(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, const FTDFluidGpuFog3DFrameInput& FrameInput, bool& bOutChanged)
	{
		if (!FrameInput.bFollowsGround)
		{
			bOutChanged = State.ColumnTerrainTarget.IsValid() || State.GroundHeightTarget.IsValid();
			ReleaseFog3DGroundTextures(State);
			return CreateFog3DEmptyColumnTerrainTexture(GraphBuilder);
		}

		if (HasFog3DColumnTerrain(State, FrameInput.Resolution))
		{
			bOutChanged = false;
			return GraphBuilder.RegisterExternalTexture(State.ColumnTerrainTarget);
		}

		bOutChanged = true;
		FRDGTextureRef ColumnTerrain = CreateFog3DPlaneTexture(GraphBuilder, FrameInput.Resolution + 2, PF_A32B32G32R32F, TEXT("TDFluidGpu.Fog3DColumnTerrain"));
		State.ColumnTerrainTarget = GraphBuilder.ConvertToExternalTexture(ColumnTerrain);
		return ColumnTerrain;
	}

	FRDGTextureRef AcquireFog3DGroundHeightTexture(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, int32 GroundSize)
	{
		const bool bRecreatesGround = !State.GroundHeightTarget.IsValid() || State.GroundSize != GroundSize;
		if (!bRecreatesGround)
		{
			return GraphBuilder.RegisterExternalTexture(State.GroundHeightTarget);
		}

		FRDGTextureRef GroundHeight = CreateFog3DPlaneTexture(GraphBuilder, GroundSize, PF_R32_FLOAT, TEXT("TDFluidGpu.Fog3DGroundHeight"));
		State.GroundHeightTarget = GraphBuilder.ConvertToExternalTexture(GroundHeight);
		return GroundHeight;
	}

	FRDGTextureRef RegisterFog3DGroundPlaneTarget(FRDGBuilder& GraphBuilder, FTextureRenderTargetResource& Target, const TCHAR* Name)
	{
		FRDGTextureRef Texture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Target.GetRenderTargetTexture(), Name));
		GraphBuilder.SetTextureAccessFinal(Texture, ERHIAccess::SRVMask);
		return Texture;
	}

	FRDGTextureRef AddFog3DConvertGroundPass(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, const FTDFluidGpuFog3DGroundCapture& GroundCapture)
	{
		const int32 GroundSize = GroundCapture.Size;
		FRDGTextureRef GroundHeight = AcquireFog3DGroundHeightTexture(GraphBuilder, State, GroundSize);
		FRDGTextureRef CaptureDepth = RegisterFog3DGroundPlaneTarget(GraphBuilder, *GroundCapture.DepthTarget, TEXT("TDFluidGpu.Fog3DGroundCaptureDepth"));

		FTDFluidGpuFog3DConvertGroundCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DConvertGroundCS::FParameters>();
		Parameters->GroundCaptureDepthIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(CaptureDepth));
		Parameters->GroundHeightOut = GraphBuilder.CreateUAV(GroundHeight);
		Parameters->GroundCaptureSize = static_cast<uint32>(GroundSize);
		Parameters->GroundDecodeOffsetCm = GroundCapture.DecodeOffsetCm;
		Parameters->GroundMissingHeightCm = GroundCapture.MissingHeightCm;
		Parameters->GroundMissingBelowFloorCm = GroundCapture.MissingBelowFloorCm;
		TShaderMapRef<FTDFluidGpuFog3DConvertGroundCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(GroundSize, GroundSize, 1), FIntVector(Fog3DGroupSizeX, Fog3DGroupSizeY, 1));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.ConvertGround"), Shader, Parameters, GroupCount);

		State.GroundMinCell = GroundCapture.MinCell;
		State.GroundSize = GroundSize;
		State.bHasGround = true;
		return GroundHeight;
	}

	void AddFog3DGroundMaterialCopyPass(FRDGBuilder& GraphBuilder, FRDGTextureRef GroundHeight, FTextureRenderTargetResource& MaterialTarget)
	{
		FRDGTextureRef MaterialTexture = RegisterFog3DGroundPlaneTarget(GraphBuilder, MaterialTarget, TEXT("TDFluidGpu.Fog3DGroundMaterial"));
		AddCopyTexturePass(GraphBuilder, GroundHeight, MaterialTexture);
	}

	FRDGTextureRef AddFog3DUploadGroundPass(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, const FTDFluidGpuFog3DGroundUpload& GroundUpload)
	{
		const int32 GroundSize = GroundUpload.Size.X;
		FRDGTextureRef GroundHeight = AcquireFog3DGroundHeightTexture(GraphBuilder, State, GroundSize);
		FRDGBufferRef UploadBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Fog3DGroundUpload"), GroundUpload.HeightsCm);

		FTDFluidGpuFog3DUploadGroundCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DUploadGroundCS::FParameters>();
		Parameters->GroundUploadIn = GraphBuilder.CreateSRV(UploadBuffer);
		Parameters->GroundHeightOut = GraphBuilder.CreateUAV(GroundHeight);
		Parameters->GroundUploadSize = static_cast<uint32>(GroundSize);
		TShaderMapRef<FTDFluidGpuFog3DUploadGroundCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(GroundSize, GroundSize, 1), FIntVector(Fog3DGroupSizeX, Fog3DGroupSizeY, 1));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.UploadGround"), Shader, Parameters, GroupCount);

		State.GroundMinCell = GroundUpload.MinCell;
		State.GroundSize = GroundSize;
		State.bHasGround = true;
		return GroundHeight;
	}

	FRDGTextureRef AddFog3DGroundPasses(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		if (IsFog3DGroundUploading(FrameInput))
		{
			return AddFog3DUploadGroundPass(GraphBuilder, State, *FrameInput.GroundUpload);
		}

		if (IsFog3DGroundCapturing(FrameInput))
		{
			CSV_SCOPED_TIMING_STAT(TDFog3D, GroundCaptureConvert);
			RDG_EVENT_SCOPE_STAT(GraphBuilder, TDFog3DGroundCaptureConvert, "TDFog3DGroundCaptureConvert");
			FRDGTextureRef GroundHeight = AddFog3DConvertGroundPass(GraphBuilder, State, FrameInput.GroundCapture);
			if (FrameInput.GroundMaterialTarget)
			{
				AddFog3DGroundMaterialCopyPass(GraphBuilder, GroundHeight, *FrameInput.GroundMaterialTarget);
			}
			return GroundHeight;
		}

		if (!State.bHasGround || !State.GroundHeightTarget.IsValid())
		{
			return nullptr;
		}
		return GraphBuilder.RegisterExternalTexture(State.GroundHeightTarget);
	}

	FRDGTextureRef CreateFog3DEmptyGroundTexture(FRDGBuilder& GraphBuilder)
	{
		FRDGTextureRef EmptyGround = CreateFog3DPlaneTexture(GraphBuilder, 1, PF_R32_FLOAT, TEXT("TDFluidGpu.Fog3DEmptyGround"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EmptyGround), 0.0f);
		return EmptyGround;
	}

	FIntPoint MakeFog3DGroundTexelOffset(const FTDFluidGpuFog3DState& State, const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		if (!FrameInput.bFollowsGround)
		{
			return FIntPoint::ZeroValue;
		}
		return FIntPoint(
			static_cast<int32>(static_cast<int64>(FrameInput.WindowMinCell.X) - State.GroundMinCell.X),
			static_cast<int32>(static_cast<int64>(FrameInput.WindowMinCell.Y) - State.GroundMinCell.Y));
	}

	void AddFog3DBakeTerrainPass(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DCommonParameters& CommonParameters, const FTDFluidGpuFog3DFrameInput& FrameInput, const FTDFluidGpuFog3DState& State, FRDGTextureRef GroundHeight, FRDGTextureRef ColumnTerrain)
	{
		FTDFluidGpuFog3DBakeTerrainCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DBakeTerrainCS::FParameters>();
		Parameters->Common = CommonParameters;
		Parameters->GroundHeightIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(GroundHeight ? GroundHeight : CreateFog3DEmptyGroundTexture(GraphBuilder)));
		Parameters->ColumnTerrainOut = GraphBuilder.CreateUAV(ColumnTerrain);
		Parameters->GroundTexelOffset = MakeFog3DGroundTexelOffset(State, FrameInput);
		Parameters->GroundFollow = FrameInput.bFollowsGround ? 1u : 0u;
		Parameters->GroundWallStepCm = FrameInput.GroundWallStepCm;
		TShaderMapRef<FTDFluidGpuFog3DBakeTerrainCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const int32 ColumnTerrainSize = CommonParameters.Resolution + 2;
		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(ColumnTerrainSize, ColumnTerrainSize, 1), FIntVector(Fog3DGroupSizeX, Fog3DGroupSizeY, 1));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.BakeTerrain"), Shader, Parameters, GroupCount);
	}

	void ClearFog3DStateTextures(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DGraphTextures& Textures)
	{
		const FLinearColor ZeroVector(0.0f, 0.0f, 0.0f, 0.0f);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Textures.Wind[0]), ZeroVector);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Textures.Wind[1]), ZeroVector);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Textures.DisplacementFill[0]), ZeroVector);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Textures.DisplacementFill[1]), ZeroVector);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Textures.Pressure), 0.0f);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Textures.Divergence), 0.0f);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Textures.CellBlocked), 0u);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Textures.FaceFlags), 0u);
	}

	FRDGTextureRef RegisterFog3DOutputTexture(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		FRHITexture* OutputRHI = FindFog3DOutputTexture(FrameInput);
		if (!OutputRHI)
		{
			return CreateFog3DTexture(GraphBuilder, FrameInput.Resolution, PF_FloatRGBA, TEXT("TDFluidGpu.Fog3DOutputScratch"));
		}

		FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(OutputRHI, TEXT("TDFluidGpu.Fog3DOutput")));
		GraphBuilder.SetTextureAccessFinal(OutputTexture, ERHIAccess::SRVMask);
		return OutputTexture;
	}

	FTDFluidGpuFog3DCommonParameters MakeFog3DCommonParameters(int32 Resolution, float CellSizeCm, float LayerHeightCm, const FIntPoint& WindowMinCell, const FIntPoint& PreviousWindowMinCell)
	{
		FTDFluidGpuFog3DCommonParameters Parameters;
		Parameters.Resolution = Resolution;
		Parameters.CellSizeCm = CellSizeCm;
		Parameters.LayerHeightCm = LayerHeightCm;
		Parameters.WindowMinCell = WindowMinCell;
		Parameters.PreviousWindowMinCell = PreviousWindowMinCell;
		return Parameters;
	}

	float ComputeFog3DDecayFactor(double RatePerSec, double DeltaSeconds, bool bApplies)
	{
		if (!bApplies)
		{
			return 1.0f;
		}

		return static_cast<float>(FMath::Exp(-RatePerSec * DeltaSeconds));
	}

	double ComputeFog3DSmoothStep(double Edge0, double Edge1, double Value)
	{
		const double Ratio = FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
		return Ratio * Ratio * (3.0 - 2.0 * Ratio);
	}

	float ComputeFog3DEdgeBandFactor(const FTDFluidGpuFog3DFrameInput& FrameInput, int32 EdgeDistanceCells)
	{
		const int32 BandCells = FrameInput.Tuning.EdgeBandCells;
		if (!FrameInput.TestKnobs.bAppliesEdgeBand || BandCells <= 0 || EdgeDistanceCells >= BandCells)
		{
			return 1.0f;
		}

		const double Fade = ComputeFog3DSmoothStep(0.0, static_cast<double>(BandCells), static_cast<double>(EdgeDistanceCells) + 0.5);
		return static_cast<float>(FMath::Exp(-static_cast<double>(FrameInput.Tuning.EdgeBandAbsorptionPerSec) * (1.0 - Fade) * FrameInput.DeltaSeconds));
	}

	void AddFog3DBakePasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DCommonParameters& CommonParameters, const FTDFluidGpuFog3DFrameInput& FrameInput, const FTDFluidGpuFog3DState& State, FRDGTextureRef GroundHeight, const FTDFluidGpuFog3DGraphTextures& Textures)
	{
		if (FrameInput.bFollowsGround)
		{
			AddFog3DBakeTerrainPass(GraphBuilder, CommonParameters, FrameInput, State, GroundHeight, Textures.ColumnTerrain);
		}

		const FIntVector GroupCount = GetFog3DGroupCount(CommonParameters.Resolution);
		FRDGBufferRef ObstacleBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Fog3DObstacles"), FrameInput.Obstacles);

		FTDFluidGpuFog3DBakeBlockedCS::FParameters* BlockedParameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DBakeBlockedCS::FParameters>();
		BlockedParameters->Common = CommonParameters;
		BlockedParameters->Obstacles = GraphBuilder.CreateSRV(ObstacleBuffer);
		BlockedParameters->ColumnTerrainIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.ColumnTerrain));
		BlockedParameters->CellBlockedOut = GraphBuilder.CreateUAV(Textures.CellBlocked);
		BlockedParameters->ObstacleCount = static_cast<uint32>(FrameInput.Obstacles.Num());
		BlockedParameters->GroundFollow = FrameInput.bFollowsGround ? 1u : 0u;
		TShaderMapRef<FTDFluidGpuFog3DBakeBlockedCS> BlockedShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.BakeBlocked"), BlockedShader, BlockedParameters, GroupCount);

		FTDFluidGpuFog3DBakeFaceFlagsCS::FParameters* FaceParameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DBakeFaceFlagsCS::FParameters>();
		FaceParameters->Common = CommonParameters;
		FaceParameters->CellBlockedIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.CellBlocked));
		FaceParameters->FaceFlagsOut = GraphBuilder.CreateUAV(Textures.FaceFlags);
		TShaderMapRef<FTDFluidGpuFog3DBakeFaceFlagsCS> FaceShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.BakeFaceFlags"), FaceShader, FaceParameters, GroupCount);
	}

	void AddFog3DCleanPass(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DCommonParameters& CommonParameters, const FTDFluidGpuFog3DGraphTextures& Textures, FTDFluidGpuFog3DState& State)
	{
		const int32 ReadWindIndex = State.CurrentWindIndex;
		const int32 WriteWindIndex = 1 - ReadWindIndex;
		FTDFluidGpuFog3DCleanCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DCleanCS::FParameters>();
		Parameters->Common = CommonParameters;
		Parameters->WindIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.Wind[ReadWindIndex]));
		Parameters->FaceFlagsIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.FaceFlags));
		Parameters->WindOut = GraphBuilder.CreateUAV(Textures.Wind[WriteWindIndex]);
		Parameters->DisplacementFillOut = GraphBuilder.CreateUAV(Textures.DisplacementFill[State.CurrentDisplacementFillIndex]);
		Parameters->PressureOut = GraphBuilder.CreateUAV(Textures.Pressure);
		TShaderMapRef<FTDFluidGpuFog3DCleanCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.Clean"), Shader, Parameters, GetFog3DGroupCount(CommonParameters.Resolution));
		State.CurrentWindIndex = WriteWindIndex;
	}

	TArray<uint32> MakeFog3DFillOperationUpload(const TArray<uint8>& FillOperations)
	{
		TArray<uint32> Upload;
		Upload.Reserve(FillOperations.Num());
		for (const uint8 Operation : FillOperations)
		{
			Upload.Add(static_cast<uint32>(Operation));
		}
		return Upload;
	}

	bool HasFog3DStateOverride(const FTDFluidGpuFog3DFrameInput& FrameInput)
	{
		return FrameInput.StateOverride && (!FrameInput.StateOverride->WindFaces.IsEmpty() || !FrameInput.StateOverride->FillOperations.IsEmpty());
	}

	void AddFog3DOverridePass(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DCommonParameters& CommonParameters, const FTDFluidGpuFog3DStateOverride& StateOverride, const FTDFluidGpuFog3DGraphTextures& Textures, FTDFluidGpuFog3DState& State)
	{
		const int32 ReadWindIndex = State.CurrentWindIndex;
		const int32 ReadDisplacementFillIndex = State.CurrentDisplacementFillIndex;
		FRDGBufferRef WindOverrideBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Fog3DWindOverride"), StateOverride.WindFaces);
		FRDGBufferRef FillOperationBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Fog3DFillOperation"), MakeFog3DFillOperationUpload(StateOverride.FillOperations));

		FTDFluidGpuFog3DOverrideCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DOverrideCS::FParameters>();
		Parameters->Common = CommonParameters;
		Parameters->WindIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.Wind[ReadWindIndex]));
		Parameters->DisplacementFillIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.DisplacementFill[ReadDisplacementFillIndex]));
		Parameters->FaceFlagsIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.FaceFlags));
		Parameters->WindOverride = GraphBuilder.CreateSRV(WindOverrideBuffer);
		Parameters->FillOperation = GraphBuilder.CreateSRV(FillOperationBuffer);
		Parameters->WindOut = GraphBuilder.CreateUAV(Textures.Wind[1 - ReadWindIndex]);
		Parameters->DisplacementFillOut = GraphBuilder.CreateUAV(Textures.DisplacementFill[1 - ReadDisplacementFillIndex]);
		Parameters->HasWindOverride = StateOverride.WindFaces.IsEmpty() ? 0u : 1u;
		Parameters->HasFillOperation = StateOverride.FillOperations.IsEmpty() ? 0u : 1u;
		TShaderMapRef<FTDFluidGpuFog3DOverrideCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.Override"), Shader, Parameters, GetFog3DGroupCount(CommonParameters.Resolution));
		State.CurrentWindIndex = 1 - ReadWindIndex;
		State.CurrentDisplacementFillIndex = 1 - ReadDisplacementFillIndex;
	}

	void FillFog3DAdvectFactors(const FTDFluidGpuFog3DFrameInput& FrameInput, FTDFluidGpuFog3DAdvectCS::FParameters& Parameters)
	{
		const FTDFluidGpuFog3DTuning& Tuning = FrameInput.Tuning;
		const bool bAppliesRefill = FrameInput.TestKnobs.bAppliesRefill;
		Parameters.Mode = FrameInput.Mode;
		Parameters.DeltaSeconds = FrameInput.DeltaSeconds;
		Parameters.WindRefillFactor = ComputeFog3DDecayFactor(Tuning.WindRefillPerSec, FrameInput.DeltaSeconds, bAppliesRefill);
		Parameters.DisplacementRefillFactorHorizontal = ComputeFog3DDecayFactor(Tuning.DisplacementRefillHorizontalPerSec, FrameInput.DeltaSeconds, bAppliesRefill);
		Parameters.DisplacementRefillFactorVertical = ComputeFog3DDecayFactor(Tuning.DisplacementRefillVerticalPerSec, FrameInput.DeltaSeconds, bAppliesRefill);
		Parameters.FillRefillFactor = ComputeFog3DDecayFactor(Tuning.FillRefillPerSec, FrameInput.DeltaSeconds, bAppliesRefill);
		Parameters.EdgeBandCells = FrameInput.TestKnobs.bAppliesEdgeBand ? Tuning.EdgeBandCells : 0;
		Parameters.MoveLimitCm = FVector3f(Tuning.MoveLimitHorizontalCm, Tuning.MoveLimitHorizontalCm, Tuning.MoveLimitVerticalCm);
		for (int32 VectorIndex = 0; VectorIndex < Fog3DEdgeBandFactorVectorCount; ++VectorIndex)
		{
			const int32 FirstCell = VectorIndex * 4;
			Parameters.EdgeBandFactors[VectorIndex] = FVector4f(
				ComputeFog3DEdgeBandFactor(FrameInput, FirstCell),
				ComputeFog3DEdgeBandFactor(FrameInput, FirstCell + 1),
				ComputeFog3DEdgeBandFactor(FrameInput, FirstCell + 2),
				ComputeFog3DEdgeBandFactor(FrameInput, FirstCell + 3));
		}
	}

	FRDGBufferRef AddFog3DInputFramePass(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DCommonParameters& CommonParameters, const FTDFluidGpuFog3DFrameInput& FrameInput, FRDGBufferRef SphereBuffer, FRDGTextureRef ColumnTerrain)
	{
		if (!FrameInput.bFollowsGround || FrameInput.Spheres.IsEmpty())
		{
			return CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Fog3DSphereFramesEmpty"), TArray<FTDFluidGpuFog3DSphereFrame>());
		}

		const uint32 SphereCount = static_cast<uint32>(FrameInput.Spheres.Num());
		FRDGBufferRef FrameBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FTDFluidGpuFog3DSphereFrame), SphereCount), TEXT("TDFluidGpu.Fog3DSphereFrames"));
		FTDFluidGpuFog3DInputFrameCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DInputFrameCS::FParameters>();
		Parameters->Common = CommonParameters;
		Parameters->Spheres = GraphBuilder.CreateSRV(SphereBuffer);
		Parameters->ColumnTerrainIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(ColumnTerrain));
		Parameters->SphereFramesOut = GraphBuilder.CreateUAV(FrameBuffer);
		Parameters->SphereCount = SphereCount;
		TShaderMapRef<FTDFluidGpuFog3DInputFrameCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(FIntVector(static_cast<int32>(SphereCount), 1, 1), FIntVector(Fog3DInputFrameGroupSize, 1, 1));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.InputFrame"), Shader, Parameters, GroupCount);

		if (FrameInput.SphereFrameReadback)
		{
			AddEnqueueCopyPass(GraphBuilder, FrameInput.SphereFrameReadback, FrameBuffer, SphereCount * sizeof(FTDFluidGpuFog3DSphereFrame));
		}
		return FrameBuffer;
	}

	FRDGBufferRef AcquireFog3DInputReachBuffer(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, int32 Resolution)
	{
		const uint32 CellCount = static_cast<uint32>(GetFog3DCellCount(Resolution));
		if (State.InputReachBuffer.IsValid() && State.InputReachBuffer->Desc.NumElements == CellCount)
		{
			return GraphBuilder.RegisterExternalBuffer(State.InputReachBuffer);
		}

		FRDGBufferRef ReachBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), CellCount), TEXT("TDFluidGpu.Fog3DInputReach"));
		State.InputReachBuffer = GraphBuilder.ConvertToExternalBuffer(ReachBuffer);
		return ReachBuffer;
	}

	FRDGBufferRef AddFog3DInputReachPass(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DCommonParameters& CommonParameters, const FTDFluidGpuFog3DFrameInput& FrameInput, const FTDFluidGpuFog3DGraphTextures& Textures, FRDGBufferRef SphereBuffer, FTDFluidGpuFog3DState& State)
	{
		if (!FrameInput.bOccludesInputBehindObstacles)
		{
			State.InputReachBuffer.SafeRelease();
			return CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Fog3DInputReachEmpty"), TArray<uint32>());
		}

		FRDGBufferRef ReachBuffer = AcquireFog3DInputReachBuffer(GraphBuilder, State, CommonParameters.Resolution);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ReachBuffer), 0u);
		if (FrameInput.Spheres.IsEmpty())
		{
			return ReachBuffer;
		}

		FRDGBufferRef AnchorBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Fog3DSphereAnchors"), FrameInput.SphereAnchors);
		FTDFluidGpuFog3DInputReachCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DInputReachCS::FParameters>();
		Parameters->Common = CommonParameters;
		Parameters->FaceFlagsIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.FaceFlags));
		Parameters->CellBlockedIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.CellBlocked));
		Parameters->ColumnTerrainIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.ColumnTerrain));
		Parameters->Spheres = GraphBuilder.CreateSRV(SphereBuffer);
		Parameters->SphereAnchors = GraphBuilder.CreateSRV(AnchorBuffer);
		Parameters->InputReachOut = GraphBuilder.CreateUAV(ReachBuffer);
		Parameters->GroundFollow = FrameInput.bFollowsGround ? 1u : 0u;
		TShaderMapRef<FTDFluidGpuFog3DInputReachCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.InputReach"), Shader, Parameters, FIntVector(FrameInput.Spheres.Num(), 1, 1));
		return ReachBuffer;
	}

	void AddFog3DAdvectPass(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DCommonParameters& CommonParameters, const FTDFluidGpuFog3DFrameInput& FrameInput, const FTDFluidGpuFog3DGraphTextures& Textures, FRDGTextureRef OutputTexture, FTDFluidGpuFog3DState& State)
	{
		const int32 ReadWindIndex = State.CurrentWindIndex;
		const int32 ReadDisplacementFillIndex = State.CurrentDisplacementFillIndex;
		FRDGBufferRef SphereBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("TDFluidGpu.Fog3DSpheres"), FrameInput.Spheres);
		FRDGBufferRef SphereFrameBuffer = AddFog3DInputFramePass(GraphBuilder, CommonParameters, FrameInput, SphereBuffer, Textures.ColumnTerrain);
		FRDGBufferRef InputReachBuffer = AddFog3DInputReachPass(GraphBuilder, CommonParameters, FrameInput, Textures, SphereBuffer, State);

		FTDFluidGpuFog3DAdvectCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DAdvectCS::FParameters>();
		Parameters->Common = CommonParameters;
		Parameters->WindIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.Wind[ReadWindIndex]));
		Parameters->DisplacementFillIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.DisplacementFill[ReadDisplacementFillIndex]));
		Parameters->FaceFlagsIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.FaceFlags));
		Parameters->Spheres = GraphBuilder.CreateSRV(SphereBuffer);
		Parameters->SphereFrames = GraphBuilder.CreateSRV(SphereFrameBuffer);
		Parameters->ColumnTerrainIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.ColumnTerrain));
		Parameters->WindOut = GraphBuilder.CreateUAV(Textures.Wind[1 - ReadWindIndex]);
		Parameters->DisplacementFillOut = GraphBuilder.CreateUAV(Textures.DisplacementFill[1 - ReadDisplacementFillIndex]);
		Parameters->OutputVolume = GraphBuilder.CreateUAV(OutputTexture);
		Parameters->SphereCount = static_cast<uint32>(FrameInput.Spheres.Num());
		Parameters->GroundFollow = FrameInput.bFollowsGround ? 1u : 0u;
		Parameters->GroundWallStepCm = FrameInput.GroundWallStepCm;
		const double WindBlendStepRepeat = TDFluidGpuFog3DWindBlendReferenceStepsPerSecond * static_cast<double>(FrameInput.DeltaSeconds);
		Parameters->ClampedTimeWindBlend = FrameInput.bUsesClampedTimeWindBlend ? 1u : 0u;
		Parameters->WindBlendStepRepeat = static_cast<float>(WindBlendStepRepeat);
		Parameters->WindBlendIsUnitStep = FMath::Abs(WindBlendStepRepeat - 1.0) <= TDFluidGpuFog3DWindBlendUnitStepTolerance ? 1u : 0u;
		Parameters->InputReachIn = GraphBuilder.CreateSRV(InputReachBuffer);
		FillFog3DAdvectFactors(FrameInput, *Parameters);
		FTDFluidGpuFog3DAdvectCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTDFluidGpuFog3DAdvectCS::FInputOcclusionDimension>(FrameInput.bOccludesInputBehindObstacles);
		TShaderMapRef<FTDFluidGpuFog3DAdvectCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.Advect"), Shader, Parameters, GetFog3DGroupCount(CommonParameters.Resolution));
		State.CurrentDisplacementFillIndex = 1 - ReadDisplacementFillIndex;
	}

	void AddFog3DDivergencePass(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DCommonParameters& CommonParameters, FRDGTextureRef Wind, FRDGTextureRef Divergence)
	{
		FTDFluidGpuFog3DDivergenceCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DDivergenceCS::FParameters>();
		Parameters->Common = CommonParameters;
		Parameters->WindIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Wind));
		Parameters->DivergenceOut = GraphBuilder.CreateUAV(Divergence);
		TShaderMapRef<FTDFluidGpuFog3DDivergenceCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.Divergence"), Shader, Parameters, GetFog3DGroupCount(CommonParameters.Resolution));
	}

	void AddFog3DGradientPass(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DCommonParameters& CommonParameters, const FTDFluidGpuFog3DGraphTextures& Textures, FRDGTextureRef WindIn, FRDGTextureRef WindOut, bool bAppliesPressure)
	{
		FTDFluidGpuFog3DGradientCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DGradientCS::FParameters>();
		Parameters->Common = CommonParameters;
		Parameters->WindIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(WindIn));
		Parameters->PressureIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.Pressure));
		Parameters->FaceFlagsIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.FaceFlags));
		Parameters->WindOut = GraphBuilder.CreateUAV(WindOut);
		Parameters->AppliesPressureGradient = bAppliesPressure ? 1u : 0u;
		TShaderMapRef<FTDFluidGpuFog3DGradientCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.Gradient"), Shader, Parameters, GetFog3DGroupCount(CommonParameters.Resolution));
	}

	void AddFog3DProjectionPasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DCommonParameters& CommonParameters, const FTDFluidGpuFog3DFrameInput& FrameInput, const FTDFluidGpuFog3DGraphTextures& Textures, const FTDFluidGpuFog3DState& State)
	{
		FRDGTextureRef CurrentWind = Textures.Wind[State.CurrentWindIndex];
		FRDGTextureRef AdvectedWind = Textures.Wind[1 - State.CurrentWindIndex];
		AddFog3DDivergencePass(GraphBuilder, CommonParameters, AdvectedWind, Textures.Divergence);
		const bool bAppliesPressure = FrameInput.TestKnobs.bAppliesPressure;
		if (bAppliesPressure)
		{
			FTDFluidGpuFog3DPressureSettings PressureSettings;
			PressureSettings.SweepCount = FrameInput.Tuning.PressureSweepCount;
			PressureSettings.OverRelaxation = FrameInput.Tuning.PressureOverRelaxation;
			AddTDFluidGpuFog3DPressurePasses(GraphBuilder, CommonParameters, Textures.Divergence, Textures.FaceFlags, Textures.Pressure, PressureSettings);
		}
		AddFog3DGradientPass(GraphBuilder, CommonParameters, Textures, AdvectedWind, CurrentWind, bAppliesPressure);
	}
}

int64 FTDFluidGpuFog3DState::EstimateStateBytes(int32 Resolution, int32 GroundSize)
{
	using namespace TDFluidGpuFog3DPrivate;

	if (Resolution <= 0)
	{
		return 0;
	}

	const int64 CellBytes = GetFog3DCellCount(Resolution) * Fog3DStateBytesPerCell;
	if (GroundSize <= 0)
	{
		return CellBytes;
	}

	const int64 ColumnTerrainSize = static_cast<int64>(Resolution) + 2;
	return CellBytes + ColumnTerrainSize * ColumnTerrainSize * Fog3DColumnTerrainBytesPerTexel + static_cast<int64>(GroundSize) * GroundSize * Fog3DGroundHeightBytesPerTexel;
}

void FTDFluidGpuFog3DState::Release()
{
	WindTargets[0].SafeRelease();
	WindTargets[1].SafeRelease();
	DisplacementFillTargets[0].SafeRelease();
	DisplacementFillTargets[1].SafeRelease();
	PressureTarget.SafeRelease();
	DivergenceTarget.SafeRelease();
	CellBlockedTarget.SafeRelease();
	FaceFlagsTarget.SafeRelease();
	ColumnTerrainTarget.SafeRelease();
	GroundHeightTarget.SafeRelease();
	InputReachBuffer.SafeRelease();
	Resolution = 0;
	CurrentWindIndex = 0;
	CurrentDisplacementFillIndex = 0;
	WindowMinCell = FIntPoint::ZeroValue;
	bHasStepped = false;
	GroundMinCell = FIntPoint::ZeroValue;
	GroundSize = 0;
	bHasGround = false;
}

FTDFluidGpuFog3DObstacle MakeTDFluidGpuFog3DUprightBox(const FVector3d& CenterWindowCm, const FVector3d& HalfExtentCm, double YawDegrees, const FTDFluidGpuFog3DInflation& Inflation)
{
	const double YawRadians = FMath::DegreesToRadians(YawDegrees);
	const double HorizontalMinimumHalfCm = static_cast<double>(Inflation.BaseHalfThicknessCm);
	const double VerticalMinimumHalfCm = static_cast<double>(Inflation.BaseHalfThicknessCm) + static_cast<double>(Inflation.UpwardExtraCm);

	FTDFluidGpuFog3DObstacle Obstacle;
	Obstacle.CenterCmYawCosine = FVector4f(
		static_cast<float>(CenterWindowCm.X),
		static_cast<float>(CenterWindowCm.Y),
		static_cast<float>(CenterWindowCm.Z),
		static_cast<float>(FMath::Cos(YawRadians)));
	Obstacle.HalfExtentCmYawSine = FVector4f(
		static_cast<float>(FMath::Max(HalfExtentCm.X, HorizontalMinimumHalfCm)),
		static_cast<float>(FMath::Max(HalfExtentCm.Y, HorizontalMinimumHalfCm)),
		static_cast<float>(FMath::Max(HalfExtentCm.Z, VerticalMinimumHalfCm)),
		static_cast<float>(FMath::Sin(YawRadians)));
	return Obstacle;
}

FTDFluidGpuFog3DSphereAnchor MakeTDFluidGpuFog3DSphereAnchor(const FVector3d& AnchorWindowCm, double CellSizeCm, int32 Resolution)
{
	FTDFluidGpuFog3DSphereAnchor Anchor;
	if (AnchorWindowCm.ContainsNaN() || !(CellSizeCm > 0.0) || Resolution <= 0)
	{
		return Anchor;
	}

	const double LastCell = static_cast<double>(Resolution - 1);
	Anchor.CellX = static_cast<int32>(FMath::Clamp(FMath::FloorToDouble(AnchorWindowCm.X / CellSizeCm), 0.0, LastCell));
	Anchor.CellY = static_cast<int32>(FMath::Clamp(FMath::FloorToDouble(AnchorWindowCm.Y / CellSizeCm), 0.0, LastCell));
	Anchor.ZCm = static_cast<float>(AnchorWindowCm.Z);
	Anchor.Flags = TDFluidGpuFog3DSphereAnchorPresentFlag;
	return Anchor;
}

bool AddTDFluidGpuFog3DPasses(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, const FTDFluidGpuFog3DFrameInput& FrameInput)
{
	using namespace TDFluidGpuFog3DPrivate;

	if (!IsFog3DFrameInputValid(FrameInput) || !IsFog3DGroundInputValid(State, FrameInput) || IsFog3DPreviousWindowStale(State, FrameInput))
	{
		return false;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "TDFluidGpuFog3D %d", FrameInput.Resolution);
	const bool bRecreatesState = !HasFog3DStateTextures(State, FrameInput.Resolution);
	FTDFluidGpuFog3DGraphTextures Textures = bRecreatesState ? CreateFog3DStateTextures(GraphBuilder, State, FrameInput.Resolution) : RegisterFog3DStateTextures(GraphBuilder, State);
	bool bGroundTexturesChanged = false;
	Textures.ColumnTerrain = PrepareFog3DColumnTerrain(GraphBuilder, State, FrameInput, bGroundTexturesChanged);
	const bool bResetsState = bRecreatesState || !State.bHasStepped || FrameInput.bResetsState || IsFog3DWindowJumpTooFar(FrameInput);
	if (bResetsState)
	{
		ClearFog3DStateTextures(GraphBuilder, Textures);
		State.CurrentWindIndex = 0;
		State.CurrentDisplacementFillIndex = 0;
	}

	const FIntPoint PreviousWindowMinCell = bResetsState ? FrameInput.WindowMinCell : FrameInput.PreviousWindowMinCell;
	const bool bRebakes = bResetsState || FrameInput.bObstaclesChanged || FrameInput.WindowMinCell != PreviousWindowMinCell || FrameInput.bGroundChanged || bGroundTexturesChanged;
	const FTDFluidGpuFog3DCommonParameters CommonParameters = MakeFog3DCommonParameters(FrameInput.Resolution, FrameInput.CellSizeCm, FrameInput.LayerHeightCm, FrameInput.WindowMinCell, PreviousWindowMinCell);
	FRDGTextureRef GroundHeight = AddFog3DGroundPasses(GraphBuilder, State, FrameInput);
	if (bRebakes)
	{
		AddFog3DBakePasses(GraphBuilder, CommonParameters, FrameInput, State, GroundHeight, Textures);
		AddFog3DCleanPass(GraphBuilder, CommonParameters, Textures, State);
	}

	if (HasFog3DStateOverride(FrameInput))
	{
		AddFog3DOverridePass(GraphBuilder, CommonParameters, *FrameInput.StateOverride, Textures, State);
	}

	FRDGTextureRef OutputTexture = RegisterFog3DOutputTexture(GraphBuilder, FrameInput);
	AddFog3DAdvectPass(GraphBuilder, CommonParameters, FrameInput, Textures, OutputTexture, State);
	if (FrameInput.Mode == 1)
	{
		State.CurrentWindIndex = 1 - State.CurrentWindIndex;
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Textures.Divergence), 0.0f);
	}
	else
	{
		AddFog3DProjectionPasses(GraphBuilder, CommonParameters, FrameInput, Textures, State);
	}

	State.WindowMinCell = FrameInput.WindowMinCell;
	State.bHasStepped = true;
	return true;
}

int64 GetTDFluidGpuFog3DDumpByteCount(int32 Resolution)
{
	using namespace TDFluidGpuFog3DPrivate;

	if (Resolution <= 0)
	{
		return 0;
	}

	return GetFog3DCellCount(Resolution) * TDFluidGpuFog3DDumpFieldCount * Fog3DDumpBytesPerValue;
}

bool AddTDFluidGpuFog3DDumpPass(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, FRHIGPUBufferReadback& Readback)
{
	using namespace TDFluidGpuFog3DPrivate;

	if (!State.bHasStepped || !HasFog3DStateTextures(State, State.Resolution))
	{
		return false;
	}

	const int64 ByteCount = GetTDFluidGpuFog3DDumpByteCount(State.Resolution);
	if (ByteCount <= 0 || ByteCount > MAX_uint32)
	{
		return false;
	}

	const FTDFluidGpuFog3DGraphTextures Textures = RegisterFog3DStateTextures(GraphBuilder, State);
	const uint32 ValueCount = static_cast<uint32>(ByteCount / Fog3DDumpBytesPerValue);
	FRDGBufferRef DumpBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), ValueCount), TEXT("TDFluidGpu.Fog3DDump"));

	FTDFluidGpuFog3DDumpCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTDFluidGpuFog3DDumpCS::FParameters>();
	Parameters->Common = MakeFog3DCommonParameters(State.Resolution, 0.0f, 0.0f, State.WindowMinCell, State.WindowMinCell);
	Parameters->WindIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.Wind[State.CurrentWindIndex]));
	Parameters->DisplacementFillIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.DisplacementFill[State.CurrentDisplacementFillIndex]));
	Parameters->PressureIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.Pressure));
	Parameters->DivergenceIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(Textures.Divergence));
	Parameters->DumpOut = GraphBuilder.CreateUAV(DumpBuffer);
	TShaderMapRef<FTDFluidGpuFog3DDumpCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TDFluidGpu.Fog3D.Dump"), Shader, Parameters, GetFog3DGroupCount(State.Resolution));
	AddEnqueueCopyPass(GraphBuilder, &Readback, DumpBuffer, static_cast<uint32>(ByteCount));
	return true;
}

bool CopyTDFluidGpuFog3DDump(FRHIGPUBufferReadback& Readback, int32 Resolution, TArray<float>& OutValues)
{
	using namespace TDFluidGpuFog3DPrivate;

	OutValues.Reset();
	const int64 ByteCount = GetTDFluidGpuFog3DDumpByteCount(Resolution);
	if (ByteCount <= 0 || ByteCount > MAX_uint32 || !Readback.IsReady())
	{
		return false;
	}

	const float* Values = static_cast<const float*>(Readback.Lock(static_cast<uint32>(ByteCount)));
	if (!Values)
	{
		return false;
	}

	const int32 ValueCount = static_cast<int32>(ByteCount / Fog3DDumpBytesPerValue);
	OutValues.SetNumUninitialized(ValueCount);
	FMemory::Memcpy(OutValues.GetData(), Values, ByteCount);
	Readback.Unlock();
	return true;
}

bool AddTDFluidGpuFog3DInputReachDumpPass(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, FRHIGPUBufferReadback& Readback)
{
	using namespace TDFluidGpuFog3DPrivate;

	if (!State.bHasStepped || !State.InputReachBuffer.IsValid())
	{
		return false;
	}

	const int64 ByteCount = GetFog3DCellCount(State.Resolution) * static_cast<int64>(sizeof(uint32));
	if (ByteCount <= 0 || ByteCount != static_cast<int64>(State.InputReachBuffer->GetSize()))
	{
		return false;
	}

	AddEnqueueCopyPass(GraphBuilder, &Readback, GraphBuilder.RegisterExternalBuffer(State.InputReachBuffer), static_cast<uint32>(ByteCount));
	return true;
}

bool CopyTDFluidGpuFog3DInputReachDump(FRHIGPUBufferReadback& Readback, int32 Resolution, TArray<uint16>& OutValues)
{
	using namespace TDFluidGpuFog3DPrivate;

	OutValues.Reset();
	const int64 CellCount = GetFog3DCellCount(Resolution);
	const int64 ByteCount = CellCount * static_cast<int64>(sizeof(uint32));
	if (CellCount <= 0 || ByteCount > MAX_uint32 || !Readback.IsReady())
	{
		return false;
	}

	const uint32* Words = static_cast<const uint32*>(Readback.Lock(static_cast<uint32>(ByteCount)));
	if (!Words)
	{
		return false;
	}

	OutValues.SetNumUninitialized(static_cast<int32>(CellCount));
	for (int32 CellIndex = 0; CellIndex < OutValues.Num(); ++CellIndex)
	{
		OutValues[CellIndex] = static_cast<uint16>(Words[CellIndex]);
	}
	Readback.Unlock();
	return true;
}
