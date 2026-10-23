#include "TDFogScatteringConsoleVariables.h"

#include "HAL/IConsoleManager.h"

namespace
{
	constexpr int32 MinimumPassAmount = 1;
	constexpr int32 MaximumPassAmount = 12;
	constexpr int32 MinimumQualityPreset = 0;
	constexpr int32 MaximumQualityPreset = 3;
	constexpr int32 MinimumFormat = -1;
	constexpr int32 MaximumFormat = 1;

	TAutoConsoleVariable<int32> CVarTDFogScattering(
		TEXT("r.TDFogScattering"),
		1,
		TEXT("Height fog scattering blur composited before depth of field. 0: off, 1: on."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarTDFogScatteringQualityPreset(
		TEXT("r.TDFogScattering.QualityPreset"),
		2,
		TEXT("Blur filter quality, clamped to 0..3. Downsample/upsample taps: 0: 4/5, 1: 8/5, 2: 8/9, 3: 13/9."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarTDFogScatteringFormat(
		TEXT("r.TDFogScattering.Format"),
		-1,
		TEXT("Pixel format of the blur pyramid. -1: inherit scene color format, 0: PF_FloatR11G11B10, 1: PF_FloatRGBA. Formats without UAV store fall back to PF_FloatRGBA."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarTDFogScatteringPassAmount(
		TEXT("r.TDFogScattering.PassAmount"),
		8,
		TEXT("Mip pyramid level count of the fog scattering blur, clamped to 1..12."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<float> CVarTDFogScatteringRadius(
		TEXT("r.TDFogScattering.Radius"),
		0.9f,
		TEXT("Upsample blend between a pyramid level and its coarser neighbour, clamped to 0..1."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<float> CVarTDFogScatteringIntensity(
		TEXT("r.TDFogScattering.Intensity"),
		1.0f,
		TEXT("Scale of the fog opacity used to blend the blurred scene color, 0 or more."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarTDFogScatteringLuminanceWeighting(
		TEXT("r.TDFogScattering.LuminanceWeighting"),
		1,
		TEXT("Weights the first downsample by inverse luminance to suppress bright pixel fireflies. 0: off, 1: on."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarTDFogScatteringSkyAtmosphere(
		TEXT("r.TDFogScattering.SkyAtmosphere"),
		0,
		TEXT("Includes the sky atmosphere aerial perspective transmittance in the blur blend. 0: off, 1: on."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<float> CVarTDFogScatteringSkyAtmosphereIntensity(
		TEXT("r.TDFogScattering.SkyAtmosphereIntensity"),
		1.0f,
		TEXT("Scale of the sky atmosphere opacity included in the blur blend, 0 or more."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarTDFogScatteringVolumetricCloud(
		TEXT("r.TDFogScattering.VolumetricCloud"),
		0,
		TEXT("Includes the volumetric cloud transmittance in the blur blend (experimental). 0: off, 1: on."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<float> CVarTDFogScatteringVolumetricCloudIntensity(
		TEXT("r.TDFogScattering.VolumetricCloudIntensity"),
		1.0f,
		TEXT("Scale of the volumetric cloud opacity included in the blur blend, 0 or more."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<float> CVarTDFogScatteringVolumetricFogIntensity(
		TEXT("r.TDFogScattering.VolumetricFogILSIntensity"),
		1.0f,
		TEXT("Scale of the volumetric fog integrated light scattering opacity included in the blur blend, 0 or more. 0 excludes volumetric fog."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarTDFogScatteringHeterogeneousVolumes(
		TEXT("r.TDFogScattering.HeterogeneousVolumes"),
		1,
		TEXT("Includes heterogeneous volume transmittance in the blur blend. 0: off, 1: on."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarTDFogScatteringDebugView(
		TEXT("r.TDFogScattering.DebugView"),
		0,
		TEXT("Debug output of the composite pass. 0: off, 1: blend weight, 2: scene depth over 100 m, 3: device depth times 100, 4: media opacity (1 minus media transmittance)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarTDFogScatteringDistanceScale(
		TEXT("r.TDFogScattering.DistanceScale"),
		1.0f,
		TEXT("Scale of the optical depth of the height fog and the volumetric fog used for the blur blend, 0 or more. 1 matches the engine fog."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<float> CVarTDFogScatteringWeightPower(
		TEXT("r.TDFogScattering.WeightPower"),
		1.0f,
		TEXT("Exponent applied to the blur blend weight, clamped to 0.1..4. Below 1 raises thin fog, 1 matches the plain fog opacity."),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	constexpr float MinimumWeightPower = 0.1f;
	constexpr float MaximumWeightPower = 4.0f;

	float ClampWeightPower(float WeightPower)
	{
		return FMath::Clamp(WeightPower, MinimumWeightPower, MaximumWeightPower);
	}

	int32 ClampQualityPreset(int32 QualityPreset)
	{
		return FMath::Clamp(QualityPreset, MinimumQualityPreset, MaximumQualityPreset);
	}

	float ClampNonNegative(float Value)
	{
		return FMath::Max(Value, 0.0f);
	}
}

bool IsTDFogScatteringEnabledOnGameThread()
{
	return CVarTDFogScattering.GetValueOnGameThread() != 0;
}

void SetTDFogScatteringEnabledOnGameThread(bool bIsEnabled)
{
	CVarTDFogScattering->Set(bIsEnabled ? 1 : 0, ECVF_SetByConsole);
}

float GetTDFogScatteringIntensityOnGameThread()
{
	return ClampNonNegative(CVarTDFogScatteringIntensity.GetValueOnGameThread());
}

void SetTDFogScatteringIntensityOnGameThread(float Intensity)
{
	CVarTDFogScatteringIntensity->Set(ClampNonNegative(Intensity), ECVF_SetByConsole);
}

int32 GetTDFogScatteringQualityPresetOnGameThread()
{
	return ClampQualityPreset(CVarTDFogScatteringQualityPreset.GetValueOnGameThread());
}

void SetTDFogScatteringQualityPresetOnGameThread(int32 QualityPreset)
{
	CVarTDFogScatteringQualityPreset->Set(ClampQualityPreset(QualityPreset), ECVF_SetByConsole);
}

float GetTDFogScatteringDistanceScaleOnGameThread()
{
	return ClampNonNegative(CVarTDFogScatteringDistanceScale.GetValueOnGameThread());
}

void SetTDFogScatteringDistanceScaleOnGameThread(float DistanceScale)
{
	CVarTDFogScatteringDistanceScale->Set(ClampNonNegative(DistanceScale), ECVF_SetByConsole);
}

float GetTDFogScatteringWeightPowerOnGameThread()
{
	return ClampWeightPower(CVarTDFogScatteringWeightPower.GetValueOnGameThread());
}

void SetTDFogScatteringWeightPowerOnGameThread(float WeightPower)
{
	CVarTDFogScatteringWeightPower->Set(ClampWeightPower(WeightPower), ECVF_SetByConsole);
}

FTDFogScatteringSettings GetTDFogScatteringSettingsOnRenderThread()
{
	FTDFogScatteringSettings Settings;
	Settings.bIsEnabled = CVarTDFogScattering.GetValueOnRenderThread() != 0;
	Settings.QualityPreset = ClampQualityPreset(CVarTDFogScatteringQualityPreset.GetValueOnRenderThread());
	Settings.Format = FMath::Clamp(CVarTDFogScatteringFormat.GetValueOnRenderThread(), MinimumFormat, MaximumFormat);
	Settings.PassAmount = FMath::Clamp(CVarTDFogScatteringPassAmount.GetValueOnRenderThread(), MinimumPassAmount, MaximumPassAmount);
	Settings.Radius = FMath::Clamp(CVarTDFogScatteringRadius.GetValueOnRenderThread(), 0.0f, 1.0f);
	Settings.Intensity = ClampNonNegative(CVarTDFogScatteringIntensity.GetValueOnRenderThread());
	Settings.bShouldWeightLuminance = CVarTDFogScatteringLuminanceWeighting.GetValueOnRenderThread() != 0;
	Settings.bShouldIncludeSkyAtmosphere = CVarTDFogScatteringSkyAtmosphere.GetValueOnRenderThread() != 0;
	Settings.SkyAtmosphereIntensity = ClampNonNegative(CVarTDFogScatteringSkyAtmosphereIntensity.GetValueOnRenderThread());
	Settings.bShouldIncludeVolumetricCloud = CVarTDFogScatteringVolumetricCloud.GetValueOnRenderThread() != 0;
	Settings.VolumetricCloudIntensity = ClampNonNegative(CVarTDFogScatteringVolumetricCloudIntensity.GetValueOnRenderThread());
	Settings.VolumetricFogIntensity = ClampNonNegative(CVarTDFogScatteringVolumetricFogIntensity.GetValueOnRenderThread());
	Settings.bShouldIncludeHeterogeneousVolumes = CVarTDFogScatteringHeterogeneousVolumes.GetValueOnRenderThread() != 0;
	Settings.DistanceScale = ClampNonNegative(CVarTDFogScatteringDistanceScale.GetValueOnRenderThread());
	Settings.WeightPower = ClampWeightPower(CVarTDFogScatteringWeightPower.GetValueOnRenderThread());
	Settings.DebugView = CVarTDFogScatteringDebugView.GetValueOnRenderThread();
	return Settings;
}
