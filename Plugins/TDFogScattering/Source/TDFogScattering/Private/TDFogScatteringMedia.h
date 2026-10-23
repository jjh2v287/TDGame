#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "ShaderParameterMacros.h"
#include "TDFogScatteringConsoleVariables.h"

class FRDGBuilder;
class FSceneView;

BEGIN_SHADER_PARAMETER_STRUCT(FTDFogScatteringMediaParameters, )
	SHADER_PARAMETER_SAMPLER(SamplerState, MediaSampler)
	SHADER_PARAMETER(float, MediaSkyAtmosphereIntensity)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, MediaVolumetricFogTexture)
	SHADER_PARAMETER(float, MediaVolumetricFogIntensity)
	SHADER_PARAMETER(float, MediaVolumetricFogStartDistance)
	SHADER_PARAMETER(float, MediaVolumetricFogRendered)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MediaVolumetricCloudTexture)
	SHADER_PARAMETER(float, MediaVolumetricCloudIntensity)
	SHADER_PARAMETER(FVector2f, MediaVolumetricCloudUVScale)
	SHADER_PARAMETER(FVector2f, MediaVolumetricCloudUVMax)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MediaHeterogeneousVolumeTexture)
	SHADER_PARAMETER(float, MediaHeterogeneousVolumeIntensity)
	SHADER_PARAMETER(FIntPoint, MediaHeterogeneousVolumeTextureSize)
	SHADER_PARAMETER(uint32, MediaHeterogeneousVolumeDownsampleFactor)
END_SHADER_PARAMETER_STRUCT()

void SetupTDFogScatteringMediaParameters(FRDGBuilder& GraphBuilder, const FSceneView& View, const FTDFogScatteringSettings& Settings, FTDFogScatteringMediaParameters& OutParameters);
