#include "Combat/Feedback/TDCombatFeedbackSettings.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<float> CVarFeedbackShakeScale(
		TEXT("TD.Feedback.ShakeScale"),
		1.f,
		TEXT("Scales combat camera shake, directional kicks and hit vibration from 0 to 1."));

	TAutoConsoleVariable<int32> CVarFeedbackReduceFlashes(
		TEXT("TD.Feedback.ReduceFlashes"),
		0,
		TEXT("Halves combat hit flash and damage vignette intensity when set to 1."));

	TAutoConsoleVariable<int32> CVarFeedbackSlowMotion(
		TEXT("TD.Feedback.SlowMo"),
		1,
		TEXT("Allows combat slow motion when set to 1."));

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
	AddFeedbackSoftObjectPath(MeleeSlashTrailEffect, OutPaths);
	for (const TPair<ETDDamageElement, FTDFeedbackSoundSet>& SoundSet : ImpactSounds)
	{
		AddFeedbackSoundSetPaths(SoundSet.Value, OutPaths);
	}
	AddFeedbackSoundSetPaths(CriticalSound, OutPaths);
	AddFeedbackSoundSetPaths(KillSound, OutPaths);
	AddFeedbackSoundSetPaths(PlayerHurtSound, OutPaths);
	AddFeedbackSoundSetPaths(MeleeSwingSound, OutPaths);
	AddFeedbackSoundSetPaths(PlayerHitSounds, OutPaths);
	AddFeedbackSoundSetPaths(MonsterSwingSounds, OutPaths);
	AddFeedbackSoundSetPaths(PhaseAccentSound, OutPaths);
	AddFeedbackSoundSetPaths(StatusTickSounds, OutPaths);
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

float UTDCombatFeedbackSettings::GetEffectiveShakeScale() const
{
	return FMath::Clamp(CameraShakeScale, 0.f, 1.f) * FMath::Clamp(CVarFeedbackShakeScale.GetValueOnGameThread(), 0.f, 1.f);
}

float UTDCombatFeedbackSettings::GetFlashIntensityScale() const
{
	return CVarFeedbackReduceFlashes.GetValueOnGameThread() != 0 ? ReducedFlashScale : 1.f;
}

bool UTDCombatFeedbackSettings::IsSlowMotionEnabled() const
{
	return CVarFeedbackSlowMotion.GetValueOnGameThread() != 0;
}
