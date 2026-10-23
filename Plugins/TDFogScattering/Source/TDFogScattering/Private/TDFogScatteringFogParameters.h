#pragma once

#include "CoreMinimal.h"

class FSceneView;

struct FTDFogScatteringFogParameters
{
	FVector4f ExponentialFogParameters = FVector4f(0.0f, 1.0f, 1.0f, 0.0f);
	FVector4f ExponentialFogParameters2 = FVector4f(0.0f, 1.0f, 0.0f, 0.0f);
	FVector4f ExponentialFogParameters3 = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	float FogMaxOpacity = 1.0f;
	float FogEndDistance = 0.0f;
	float FogOnlyOnRenderedOpaque = 0.0f;
};

bool TryGetTDFogScatteringFogParameters(const FSceneView& View, FTDFogScatteringFogParameters& OutParameters);
