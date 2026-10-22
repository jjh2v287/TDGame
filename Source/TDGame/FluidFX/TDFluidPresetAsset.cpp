#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidPresetTuningSubsystem.h"

FTDFluidSimulationConfig UTDFluidPresetAsset::ResolveEffectiveConfig() const
{
	if (PresetKind == ETDFluidPresetKind::Custom || bOverridePresetDefaults)
	{
		return SimulationConfig;
	}
	return BuildConfigForPresetKind(PresetKind);
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
