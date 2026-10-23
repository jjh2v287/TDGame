#pragma once

#include "CoreMinimal.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "TDFogScatteringConsoleVariables.h"
#include "TDFogScatteringFogParameters.h"

class FRDGBuilder;
class FSceneView;

FScreenPassTexture AddTDFogScatteringPasses(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs,
	const FTDFogScatteringSettings& Settings,
	const FTDFogScatteringFogParameters& FogParameters);
