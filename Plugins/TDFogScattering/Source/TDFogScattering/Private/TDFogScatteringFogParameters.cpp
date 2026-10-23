#include "TDFogScatteringFogParameters.h"

#include "SceneRendering.h"
#include "SceneView.h"

bool TryGetTDFogScatteringFogParameters(const FSceneView& View, FTDFogScatteringFogParameters& OutParameters)
{
	if (!View.bIsViewInfo)
	{
		return false;
	}

	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);
	if (ViewInfo.ExponentialFogParameters3.X <= 0.0f && ViewInfo.ExponentialFogParameters2.Z <= 0.0f)
	{
		return false;
	}

	OutParameters.ExponentialFogParameters = ViewInfo.ExponentialFogParameters;
	OutParameters.ExponentialFogParameters2 = ViewInfo.ExponentialFogParameters2;
	OutParameters.ExponentialFogParameters3 = ViewInfo.ExponentialFogParameters3;
	OutParameters.FogMaxOpacity = ViewInfo.FogMaxOpacity;
	OutParameters.FogEndDistance = ViewInfo.FogEndDistance;
	OutParameters.FogOnlyOnRenderedOpaque = ViewInfo.bFogOnlyOnRenderedOpaque ? 1.0f : 0.0f;
	return true;
}
