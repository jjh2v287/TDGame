#include "TDFogScatteringViewExtension.h"

#include "SceneInterface.h"
#include "SceneView.h"
#include "TDFogScatteringConsoleVariables.h"
#include "TDFogScatteringFogParameters.h"
#include "TDFogScatteringPasses.h"

namespace
{
	bool ShouldSkipTDFogScattering(const FSceneView& View, const FTDFogScatteringSettings& Settings)
	{
		if (!Settings.bIsEnabled || Settings.Intensity <= 0.0f)
		{
			return true;
		}

		if (View.bIsReflectionCapture || View.Family == nullptr)
		{
			return true;
		}

		const FSceneViewFamily& Family = *View.Family;
		if (Family.EngineShowFlags.PathTracing || !Family.EngineShowFlags.Fog)
		{
			return true;
		}

		return Family.Scene == nullptr || !Family.Scene->HasAnyExponentialHeightFog();
	}
}

FTDFogScatteringViewExtension::FTDFogScatteringViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

bool FTDFogScatteringViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return IsTDFogScatteringEnabledOnGameThread();
}

void FTDFogScatteringViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& View, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (Pass != EPostProcessingPass::BeforeDOF)
	{
		return;
	}

	const FTDFogScatteringSettings Settings = GetTDFogScatteringSettingsOnRenderThread();
	if (ShouldSkipTDFogScattering(View, Settings))
	{
		return;
	}

	FTDFogScatteringFogParameters FogParameters;
	if (!TryGetTDFogScatteringFogParameters(View, FogParameters))
	{
		return;
	}

	InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateLambda(
		[Settings, FogParameters](FRDGBuilder& GraphBuilder, const FSceneView& CallbackView, const FPostProcessMaterialInputs& Inputs)
		{
			return AddTDFogScatteringPasses(GraphBuilder, CallbackView, Inputs, Settings, FogParameters);
		}));
}
