#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "ShaderParameterMacros.h"

BEGIN_SHADER_PARAMETER_STRUCT(FTDFluidGpuFog3DCommonParameters, )
	SHADER_PARAMETER(int32, Resolution)
	SHADER_PARAMETER(float, CellSizeCm)
	SHADER_PARAMETER(float, LayerHeightCm)
	SHADER_PARAMETER(FIntPoint, WindowMinCell)
	SHADER_PARAMETER(FIntPoint, PreviousWindowMinCell)
END_SHADER_PARAMETER_STRUCT()

struct FTDFluidGpuFog3DPressureSettings
{
	int32 SweepCount = 0;
	float OverRelaxation = 1.0f;
};

void AddTDFluidGpuFog3DPressurePasses(FRDGBuilder& GraphBuilder, const FTDFluidGpuFog3DCommonParameters& CommonParameters, FRDGTextureRef Divergence, FRDGTextureRef FaceFlags, FRDGTextureRef Pressure, const FTDFluidGpuFog3DPressureSettings& Settings);
