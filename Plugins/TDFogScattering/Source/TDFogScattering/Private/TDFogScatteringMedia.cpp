#include "TDFogScatteringMedia.h"

#include "HAL/IConsoleManager.h"
#include "RHIStaticStates.h"
#include "SceneInterface.h"
#include "SceneManagement.h"
#include "SceneRendering.h"
#include "SceneView.h"
#include "SystemTextures.h"

namespace
{
	constexpr int32 SupportedVolumetricRenderTargetMode = 0;
	constexpr int32 MinimumHeterogeneousVolumeDownsampleFactor = 1;
	constexpr int32 MaximumHeterogeneousVolumeDownsampleFactor = 8;

	float GetMediumIntensity(bool bShouldIncludeMedium, float Intensity)
	{
		return bShouldIncludeMedium ? FMath::Max(Intensity, 0.0f) : 0.0f;
	}

	bool IsVolumetricRenderTargetModeSupported()
	{
		IConsoleManager& ConsoleManager = IConsoleManager::Get();
		TConsoleVariableData<int32>* CloudVariable = ConsoleManager.FindTConsoleVariableDataInt(TEXT("r.VolumetricCloud"));
		if (CloudVariable != nullptr && CloudVariable->GetValueOnRenderThread() <= 0)
		{
			return false;
		}

		TConsoleVariableData<int32>* EnabledVariable = ConsoleManager.FindTConsoleVariableDataInt(TEXT("r.VolumetricRenderTarget"));
		if (EnabledVariable != nullptr && EnabledVariable->GetValueOnRenderThread() == 0)
		{
			return false;
		}

		TConsoleVariableData<int32>* ModeVariable = ConsoleManager.FindTConsoleVariableDataInt(TEXT("r.VolumetricRenderTarget.Mode"));
		return ModeVariable != nullptr && ModeVariable->GetValueOnRenderThread() == SupportedVolumetricRenderTargetMode;
	}

	bool IsVolumetricCloudRenderedForView(const FSceneView& View)
	{
		if (View.State == nullptr || View.Family == nullptr || View.Family->Scene == nullptr)
		{
			return false;
		}

		const FEngineShowFlags& ShowFlags = View.Family->EngineShowFlags;
		if (!View.IsPerspectiveProjection() || !ShowFlags.Cloud || !ShowFlags.Atmosphere)
		{
			return false;
		}

		const FSceneInterface* Scene = View.Family->Scene;
		return Scene->GetVolumetricCloudSceneInfo() != nullptr && IsVolumetricRenderTargetModeSupported();
	}

	uint32 GetHeterogeneousVolumeCompositeDownsampleFactor()
	{
		IConsoleManager& ConsoleManager = IConsoleManager::Get();
		TConsoleVariableData<int32>* UpsampleVariable = ConsoleManager.FindTConsoleVariableDataInt(TEXT("r.HeterogeneousVolumes.Upsample"));
		if (UpsampleVariable == nullptr || FMath::Clamp(UpsampleVariable->GetValueOnRenderThread(), 0, 2) != 0)
		{
			return 1;
		}

		TConsoleVariableData<float>* DownsampleVariable = ConsoleManager.FindTConsoleVariableDataFloat(TEXT("r.HeterogeneousVolumes.DownsampleFactor"));
		if (DownsampleVariable == nullptr)
		{
			return 1;
		}

		const int32 DownsampleFactor = FMath::TruncToInt(DownsampleVariable->GetValueOnRenderThread());
		return static_cast<uint32>(FMath::Clamp(DownsampleFactor, MinimumHeterogeneousVolumeDownsampleFactor, MaximumHeterogeneousVolumeDownsampleFactor));
	}

	void FillVolumetricCloudParameters(FRDGBuilder& GraphBuilder, const FSceneView& View, const FTDFogScatteringSettings& Settings, FTDFogScatteringMediaParameters& OutParameters)
	{
		const float Intensity = GetMediumIntensity(Settings.bShouldIncludeVolumetricCloud, Settings.VolumetricCloudIntensity);
		if (Intensity <= 0.0f || !IsVolumetricCloudRenderedForView(View))
		{
			return;
		}

		FRDGTextureRef CloudTexture = View.State->GetVolumetricCloudTexture(GraphBuilder);
		if (CloudTexture == nullptr)
		{
			return;
		}

		OutParameters.MediaVolumetricCloudTexture = CloudTexture;
		OutParameters.MediaVolumetricCloudIntensity = Intensity;
		OutParameters.MediaVolumetricCloudUVScale = View.State->GetVolumetricCloudTextureUVScale();
		OutParameters.MediaVolumetricCloudUVMax = View.State->GetVolumetricCloudTextureUVMax();
	}

	void FillVolumetricFogParameters(const FViewInfo& ViewInfo, const FTDFogScatteringSettings& Settings, FTDFogScatteringMediaParameters& OutParameters)
	{
		FRDGTextureRef IntegratedLightScatteringTexture = ViewInfo.VolumetricFogResources.IntegratedLightScatteringTexture;
		if (IntegratedLightScatteringTexture == nullptr)
		{
			return;
		}

		OutParameters.MediaVolumetricFogRendered = 1.0f;
		if (Settings.VolumetricFogIntensity <= 0.0f)
		{
			return;
		}

		OutParameters.MediaVolumetricFogTexture = IntegratedLightScatteringTexture;
		OutParameters.MediaVolumetricFogIntensity = Settings.VolumetricFogIntensity;
		OutParameters.MediaVolumetricFogStartDistance = ViewInfo.VolumetricFogStartDistance;
	}

	void FillHeterogeneousVolumeParameters(const FViewInfo& ViewInfo, const FTDFogScatteringSettings& Settings, FTDFogScatteringMediaParameters& OutParameters)
	{
		FRDGTextureRef RadianceTexture = ViewInfo.HeterogeneousVolumeRadiance;
		if (!Settings.bShouldIncludeHeterogeneousVolumes || RadianceTexture == nullptr)
		{
			return;
		}

		OutParameters.MediaHeterogeneousVolumeTexture = RadianceTexture;
		OutParameters.MediaHeterogeneousVolumeIntensity = 1.0f;
		OutParameters.MediaHeterogeneousVolumeTextureSize = RadianceTexture->Desc.Extent;
		OutParameters.MediaHeterogeneousVolumeDownsampleFactor = GetHeterogeneousVolumeCompositeDownsampleFactor();
	}
}

void SetupTDFogScatteringMediaParameters(FRDGBuilder& GraphBuilder, const FSceneView& View, const FTDFogScatteringSettings& Settings, FTDFogScatteringMediaParameters& OutParameters)
{
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	OutParameters.MediaSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	OutParameters.MediaSkyAtmosphereIntensity = GetMediumIntensity(Settings.bShouldIncludeSkyAtmosphere, Settings.SkyAtmosphereIntensity);
	OutParameters.MediaVolumetricFogTexture = SystemTextures.VolumetricBlackAlphaOne;
	OutParameters.MediaVolumetricFogIntensity = 0.0f;
	OutParameters.MediaVolumetricFogStartDistance = 0.0f;
	OutParameters.MediaVolumetricFogRendered = 0.0f;
	OutParameters.MediaVolumetricCloudTexture = SystemTextures.BlackAlphaOne;
	OutParameters.MediaVolumetricCloudIntensity = 0.0f;
	OutParameters.MediaVolumetricCloudUVScale = FVector2f(1.0f, 1.0f);
	OutParameters.MediaVolumetricCloudUVMax = FVector2f(1.0f, 1.0f);
	OutParameters.MediaHeterogeneousVolumeTexture = SystemTextures.BlackAlphaOne;
	OutParameters.MediaHeterogeneousVolumeIntensity = 0.0f;
	OutParameters.MediaHeterogeneousVolumeTextureSize = FIntPoint(1, 1);
	OutParameters.MediaHeterogeneousVolumeDownsampleFactor = 1;

	FillVolumetricCloudParameters(GraphBuilder, View, Settings, OutParameters);

	if (!View.bIsViewInfo)
	{
		return;
	}

	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);
	FillVolumetricFogParameters(ViewInfo, Settings, OutParameters);
	FillHeterogeneousVolumeParameters(ViewInfo, Settings, OutParameters);
}
