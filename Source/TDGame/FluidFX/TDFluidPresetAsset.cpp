#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"

namespace
{
	template <typename TSettings>
	TSettings TDFluidBuildTunedSettings(ETDFluidPresetKind Kind)
	{
		TSettings Settings;
		UTDFluidPresetTuningSubsystem::ApplyPresetTuning(Kind, TSettings::StaticStruct(), &Settings);
		return Settings;
	}
}

bool UTDFluidPresetAsset::UsesOwnPresetCopies() const
{
	return PresetKind == ETDFluidPresetKind::Custom || bOverridePresetDefaults;
}

FTDFluidSimulationConfig UTDFluidPresetAsset::ResolveEffectiveConfig() const
{
	if (UsesOwnPresetCopies())
	{
		return SimulationConfig;
	}
	return BuildConfigForPresetKind(PresetKind);
}

FTDFluidPlaneSettings UTDFluidPresetAsset::ResolveEffectivePlaneSettings() const
{
	if (UsesOwnPresetCopies())
	{
		return PlaneSettings;
	}
	return BuildPlaneSettingsForPresetKind(PresetKind);
}

FTDFluidVolumeRenderSettings UTDFluidPresetAsset::ResolveEffectiveVolumeRender() const
{
	if (UsesOwnPresetCopies())
	{
		return VolumeRender;
	}
	return BuildVolumeRenderForPresetKind(PresetKind);
}

FTDFluidSimulationConfig UTDFluidPresetAsset::BuildConfigForPresetKind(ETDFluidPresetKind Kind)
{
	FTDFluidSimulationConfig Config;
	UTDFluidPresetTuningSubsystem::ApplyPresetTuning(Kind, FTDFluidSimulationConfig::StaticStruct(), &Config);
	return Config;
}

bool UTDFluidPresetAsset::IsFireFamilyPresetKind(ETDFluidPresetKind Kind)
{
	return static_cast<uint8>(Kind) >= static_cast<uint8>(ETDFluidPresetKind::Torch);
}

FTDFluidSourceEmitterSettings UTDFluidPresetAsset::BuildSourceEmitterForPresetKind(ETDFluidPresetKind Kind)
{
	FTDFluidSourceEmitterSettings Emitter;
	UTDFluidPresetTuningSubsystem::ApplyPresetTuning(Kind, FTDFluidSourceEmitterSettings::StaticStruct(), &Emitter);
	return Emitter;
}

FTDFluidFragmentTrailSettings UTDFluidPresetAsset::BuildFragmentTrailForPresetKind(ETDFluidPresetKind Kind)
{
	FTDFluidFragmentTrailSettings Trail;
	UTDFluidPresetTuningSubsystem::ApplyPresetTuning(Kind, FTDFluidFragmentTrailSettings::StaticStruct(), &Trail);
	return Trail;
}

FTDFluidVolumeRenderSettings UTDFluidPresetAsset::BuildVolumeRenderForPresetKind(ETDFluidPresetKind Kind)
{
	FTDFluidVolumeRenderSettings VolumeRender;
	UTDFluidPresetTuningSubsystem::ApplyPresetTuning(Kind, FTDFluidVolumeRenderSettings::StaticStruct(), &VolumeRender);
	return VolumeRender;
}

FTDFluidFlameCardLayerSettings UTDFluidPresetAsset::BuildFlameCardLayersForPresetKind(ETDFluidPresetKind Kind)
{
	FTDFluidFlameCardLayerSettings CardLayers;
	UTDFluidPresetTuningSubsystem::ApplyPresetTuning(Kind, FTDFluidFlameCardLayerSettings::StaticStruct(), &CardLayers);
	return CardLayers;
}

FTDFluidPlaneSettings UTDFluidPresetAsset::BuildPlaneSettingsForPresetKind(ETDFluidPresetKind Kind)
{
	return TDFluidBuildTunedSettings<FTDFluidPlaneSettings>(Kind);
}

FTDFluidDownstreamFeedSettings UTDFluidPresetAsset::BuildDownstreamFeedForPresetKind(ETDFluidPresetKind Kind)
{
	return TDFluidBuildTunedSettings<FTDFluidDownstreamFeedSettings>(Kind);
}

FTDFluidFragmentGlowSettings UTDFluidPresetAsset::BuildFragmentGlowForPresetKind(ETDFluidPresetKind Kind)
{
	return TDFluidBuildTunedSettings<FTDFluidFragmentGlowSettings>(Kind);
}

FTDFluidInputBrushSettings UTDFluidPresetAsset::BuildInputBrushForPresetKind(ETDFluidPresetKind Kind)
{
	return TDFluidBuildTunedSettings<FTDFluidInputBrushSettings>(Kind);
}

FTDFluidRuntimeOpsSettings UTDFluidPresetAsset::BuildRuntimeOpsForPresetKind(ETDFluidPresetKind Kind)
{
	return TDFluidBuildTunedSettings<FTDFluidRuntimeOpsSettings>(Kind);
}

FTDFluidEmberSettings UTDFluidPresetAsset::BuildEmberSettingsForPresetKind(ETDFluidPresetKind Kind)
{
	return TDFluidBuildTunedSettings<FTDFluidEmberSettings>(Kind);
}

FTDFluidFireLightSettings UTDFluidPresetAsset::BuildFireLightSettingsForPresetKind(ETDFluidPresetKind Kind)
{
	return TDFluidBuildTunedSettings<FTDFluidFireLightSettings>(Kind);
}

UTDFluidPresetAsset* UTDFluidPresetAsset::CreateTransientPreset(UObject* Outer, ETDFluidPresetKind Kind)
{
	UObject* ResolvedOuter = Outer ? Outer : GetTransientPackage();
	UTDFluidPresetAsset* Preset = NewObject<UTDFluidPresetAsset>(ResolvedOuter);
	Preset->PresetKind = Kind;
	Preset->SimulationConfig = BuildConfigForPresetKind(Kind);

	switch (Kind)
	{
	case ETDFluidPresetKind::Mud:
	case ETDFluidPresetKind::FootprintPainter:
		Preset->DefaultChannel = ETDFluidBrushChannel::ChannelR_Mud;
		break;
	case ETDFluidPresetKind::Blood:
		Preset->DefaultChannel = ETDFluidBrushChannel::ChannelG_Blood;
		break;
	case ETDFluidPresetKind::Fire:
		Preset->DefaultChannel = ETDFluidBrushChannel::ChannelB_Fire;
		break;
	case ETDFluidPresetKind::Torch:
	case ETDFluidPresetKind::Campfire:
	case ETDFluidPresetKind::Explosion:
	case ETDFluidPresetKind::Candle:
	case ETDFluidPresetKind::Fireball:
	case ETDFluidPresetKind::PlasmaExplosion:
		Preset->DefaultChannel = ETDFluidBrushChannel::ChannelB_Fire;
		break;
	case ETDFluidPresetKind::CampfireSmoke:
	case ETDFluidPresetKind::ExplosionSmoke:
	case ETDFluidPresetKind::ExplosionGroundDust:
	case ETDFluidPresetKind::SteamPuff:
	case ETDFluidPresetKind::Vortex:
	case ETDFluidPresetKind::InvertedVortex:
	case ETDFluidPresetKind::PortalVortex:
		Preset->DefaultChannel = ETDFluidBrushChannel::ChannelR_Mud;
		break;
	case ETDFluidPresetKind::Water:
	case ETDFluidPresetKind::Custom:
	default:
		Preset->DefaultChannel = ETDFluidBrushChannel::ChannelA_Water;
		break;
	}
	Preset->VolumeRender = BuildVolumeRenderForPresetKind(Kind);

	return Preset;
}
