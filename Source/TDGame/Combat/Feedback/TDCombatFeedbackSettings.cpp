#include "Combat/Feedback/TDCombatFeedbackSettings.h"

namespace
{
	template <typename AssetType>
	void AddFeedbackSoftObjectPath(const TSoftObjectPtr<AssetType>& Asset, TArray<FSoftObjectPath>& OutPaths)
	{
		if (Asset.IsNull())
		{
			return;
		}
		OutPaths.AddUnique(Asset.ToSoftObjectPath());
	}

	void AddFeedbackSoundSetPaths(const FTDFeedbackSoundSet& SoundSet, TArray<FSoftObjectPath>& OutPaths)
	{
		for (const TSoftObjectPtr<USoundBase>& Sound : SoundSet.Sounds)
		{
			AddFeedbackSoftObjectPath(Sound, OutPaths);
		}
	}
}

void UTDCombatFeedbackSettings::CollectSoftObjectPaths(TArray<FSoftObjectPath>& OutPaths) const
{
	for (const TPair<ETDDamageElement, TSoftObjectPtr<UNiagaraSystem>>& Effect : ImpactEffects)
	{
		AddFeedbackSoftObjectPath(Effect.Value, OutPaths);
	}
	AddFeedbackSoftObjectPath(CriticalImpactEffect, OutPaths);
	for (const TPair<ETDDamageElement, FTDFeedbackSoundSet>& SoundSet : ImpactSounds)
	{
		AddFeedbackSoundSetPaths(SoundSet.Value, OutPaths);
	}
	AddFeedbackSoundSetPaths(CriticalSound, OutPaths);
	AddFeedbackSoundSetPaths(KillSound, OutPaths);
	AddFeedbackSoundSetPaths(PlayerHurtSound, OutPaths);
	AddFeedbackSoundSetPaths(MeleeSwingSound, OutPaths);
	AddFeedbackSoftObjectPath(HitFlashMaterial, OutPaths);
	AddFeedbackSoftObjectPath(DamageVignetteMaterial, OutPaths);
}

FLinearColor UTDCombatFeedbackSettings::GetElementNumberColor(const ETDDamageElement Element) const
{
	switch (Element)
	{
	case ETDDamageElement::Fire:
		return FireNumberColor;
	case ETDDamageElement::Frost:
		return FrostNumberColor;
	case ETDDamageElement::Arcane:
		return ArcaneNumberColor;
	default:
		return PhysicalNumberColor;
	}
}
