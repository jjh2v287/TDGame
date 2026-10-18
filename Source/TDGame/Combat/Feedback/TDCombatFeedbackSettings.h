#pragma once

#include "CoreMinimal.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"
#include "TDCombatFeedbackSettings.generated.h"

class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;

USTRUCT(BlueprintType)
struct TDGAME_API FTDFeedbackSoundSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Sound")
	TArray<TSoftObjectPtr<USoundBase>> Sounds;

	UPROPERTY(EditAnywhere, Category="Sound", meta=(ClampMin="0.0"))
	float Volume = 1.f;

	UPROPERTY(EditAnywhere, Category="Sound", meta=(ClampMin="0.1"))
	float PitchMin = 0.94f;

	UPROPERTY(EditAnywhere, Category="Sound", meta=(ClampMin="0.1"))
	float PitchMax = 1.06f;
};

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="TD Combat Feedback"))
class TDGAME_API UTDCombatFeedbackSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	void CollectSoftObjectPaths(TArray<FSoftObjectPath>& OutPaths) const;
	FLinearColor GetElementNumberColor(ETDDamageElement Element) const;

	UPROPERTY(Config, EditAnywhere, Category="Effects")
	TMap<ETDDamageElement, TSoftObjectPtr<UNiagaraSystem>> ImpactEffects;

	UPROPERTY(Config, EditAnywhere, Category="Effects")
	TSoftObjectPtr<UNiagaraSystem> CriticalImpactEffect;

	UPROPERTY(Config, EditAnywhere, Category="Effects", meta=(ClampMin="0.0"))
	float LoopingEffectSeconds = 0.4f;

	UPROPERTY(Config, EditAnywhere, Category="Sounds")
	TMap<ETDDamageElement, FTDFeedbackSoundSet> ImpactSounds;

	UPROPERTY(Config, EditAnywhere, Category="Sounds")
	FTDFeedbackSoundSet CriticalSound;

	UPROPERTY(Config, EditAnywhere, Category="Sounds")
	FTDFeedbackSoundSet KillSound;

	UPROPERTY(Config, EditAnywhere, Category="Sounds")
	FTDFeedbackSoundSet PlayerHurtSound;

	UPROPERTY(Config, EditAnywhere, Category="Sounds")
	FTDFeedbackSoundSet MeleeSwingSound;

	UPROPERTY(Config, EditAnywhere, Category="Sounds", meta=(ClampMin="0.0", ClampMax="1.0"))
	float StatusSoundVolumeScale = 0.4f;

	UPROPERTY(Config, EditAnywhere, Category="Sounds", meta=(ClampMin="1"))
	int32 ImpactSoundMaxCount = 10;

	UPROPERTY(Config, EditAnywhere, Category="Sounds", meta=(ClampMin="1"))
	int32 AccentSoundMaxCount = 4;

	UPROPERTY(Config, EditAnywhere, Category="Sounds", meta=(ClampMin="1"))
	int32 SwingSoundMaxCount = 3;

	UPROPERTY(Config, EditAnywhere, Category="Sounds", meta=(ClampMin="0.0"))
	float SoundInnerRadiusCm = 1200.f;

	UPROPERTY(Config, EditAnywhere, Category="Sounds", meta=(ClampMin="0.0"))
	float SoundFalloffDistanceCm = 3500.f;

	UPROPERTY(Config, EditAnywhere, Category="Flash")
	TSoftObjectPtr<UMaterialInterface> HitFlashMaterial;

	UPROPERTY(Config, EditAnywhere, Category="Flash")
	FName FlashColorParameterName = TEXT("FlashColor");

	UPROPERTY(Config, EditAnywhere, Category="Flash")
	FName FlashIntensityParameterName = TEXT("FlashIntensity");

	UPROPERTY(Config, EditAnywhere, Category="Flash")
	FLinearColor MonsterFlashColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

	UPROPERTY(Config, EditAnywhere, Category="Flash")
	FLinearColor PlayerFlashColor = FLinearColor(1.f, 0.1f, 0.1f, 1.f);

	UPROPERTY(Config, EditAnywhere, Category="Flash")
	FLinearColor CriticalFlashColor = FLinearColor(1.f, 0.55f, 0.1f, 1.f);

	UPROPERTY(Config, EditAnywhere, Category="Flash", meta=(ClampMin="0.0"))
	float FlashSeconds = 0.12f;

	UPROPERTY(Config, EditAnywhere, Category="Flash", meta=(ClampMin="0.0", ClampMax="1.0"))
	float StatusFlashIntensity = 0.35f;

	UPROPERTY(Config, EditAnywhere, Category="Flash", meta=(ClampMin="0.0"))
	float FlashOverlayHoldSeconds = 1.5f;

	UPROPERTY(Config, EditAnywhere, Category="Hit Stop", meta=(ClampMin="0.0"))
	float LightHitStopSeconds = 0.05f;

	UPROPERTY(Config, EditAnywhere, Category="Hit Stop", meta=(ClampMin="0.0"))
	float HeavyHitStopSeconds = 0.09f;

	UPROPERTY(Config, EditAnywhere, Category="Hit Stop", meta=(ClampMin="0.0"))
	float AttackerHitStopSeconds = 0.f;

	UPROPERTY(Config, EditAnywhere, Category="Hit Stop", meta=(ClampMin="0.0", ClampMax="1.0"))
	float HeavyDamageRatio = 0.12f;

	UPROPERTY(Config, EditAnywhere, Category="Wobble", meta=(ClampMin="0.0"))
	float WobbleLightCm = 6.f;

	UPROPERTY(Config, EditAnywhere, Category="Wobble", meta=(ClampMin="0.0"))
	float WobbleHeavyCm = 12.f;

	UPROPERTY(Config, EditAnywhere, Category="Wobble", meta=(ClampMin="0.0"))
	float WobbleSeconds = 0.22f;

	UPROPERTY(Config, EditAnywhere, Category="Wobble", meta=(ClampMin="0.001"))
	float WobbleDecaySeconds = 0.08f;

	UPROPERTY(Config, EditAnywhere, Category="Wobble", meta=(ClampMin="0.0"))
	float WobbleFrequencyHz = 9.f;

	UPROPERTY(Config, EditAnywhere, Category="Camera")
	bool bEnableCameraShake = true;

	UPROPERTY(Config, EditAnywhere, Category="Camera")
	bool bEnableForceFeedback = true;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0", ClampMax="1.0"))
	float TraumaDealt = 0.25f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0", ClampMax="1.0"))
	float TraumaDealtHeavy = 0.4f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0", ClampMax="1.0"))
	float TraumaKill = 0.5f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0", ClampMax="1.0"))
	float TraumaReceivedMin = 0.2f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0"))
	float TraumaReceivedPerDamageRatio = 2.f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0", ClampMax="1.0"))
	float TraumaReceivedMax = 0.7f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RumbleDealtIntensity = 0.2f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0"))
	float RumbleDealtSeconds = 0.08f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RumbleReceivedIntensity = 0.5f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0"))
	float RumbleReceivedSeconds = 0.15f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RumbleKillIntensity = 0.8f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0"))
	float RumbleKillSeconds = 0.25f;

	UPROPERTY(Config, EditAnywhere, Category="Numbers", meta=(ClampMin="0.05"))
	float NumberLifetimeSeconds = 0.9f;

	UPROPERTY(Config, EditAnywhere, Category="Numbers", meta=(ClampMin="0.0"))
	float NumberRiseCm = 70.f;

	UPROPERTY(Config, EditAnywhere, Category="Numbers", meta=(ClampMin="0.0"))
	float StatusNumberMergeSeconds = 0.3f;

	UPROPERTY(Config, EditAnywhere, Category="Numbers", meta=(ClampMin="0.0"))
	float StackedNumberWindowSeconds = 0.06f;

	UPROPERTY(Config, EditAnywhere, Category="Numbers", meta=(ClampMin="0.0"))
	float StackedNumberOffset = 20.f;

	UPROPERTY(Config, EditAnywhere, Category="Numbers", meta=(ClampMin="0.0"))
	float NumberScatterRange = 16.f;

	UPROPERTY(Config, EditAnywhere, Category="Numbers", meta=(ClampMin="1.0"))
	float NormalNumberFontSize = 22.f;

	UPROPERTY(Config, EditAnywhere, Category="Numbers", meta=(ClampMin="1.0"))
	float CriticalNumberFontSize = 32.f;

	UPROPERTY(Config, EditAnywhere, Category="Numbers", meta=(ClampMin="1.0"))
	float KillNumberFontSize = 36.f;

	UPROPERTY(Config, EditAnywhere, Category="Numbers", meta=(ClampMin="1.0"))
	float StatusNumberFontSize = 16.f;

	UPROPERTY(Config, EditAnywhere, Category="Numbers", meta=(ClampMin="1.0"))
	float PlayerDamageNumberFontSize = 26.f;

	UPROPERTY(Config, EditAnywhere, Category="Numbers")
	FLinearColor PhysicalNumberColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

	UPROPERTY(Config, EditAnywhere, Category="Numbers")
	FLinearColor FireNumberColor = FLinearColor(1.f, 0.55f, 0.15f, 1.f);

	UPROPERTY(Config, EditAnywhere, Category="Numbers")
	FLinearColor FrostNumberColor = FLinearColor(0.55f, 0.85f, 1.f, 1.f);

	UPROPERTY(Config, EditAnywhere, Category="Numbers")
	FLinearColor ArcaneNumberColor = FLinearColor(0.8f, 0.5f, 1.f, 1.f);

	UPROPERTY(Config, EditAnywhere, Category="Numbers")
	FLinearColor CriticalNumberColor = FLinearColor(1.f, 0.75f, 0.1f, 1.f);

	UPROPERTY(Config, EditAnywhere, Category="Numbers")
	FLinearColor PlayerDamageNumberColor = FLinearColor(1.f, 0.25f, 0.25f, 1.f);

	UPROPERTY(Config, EditAnywhere, Category="Numbers")
	FLinearColor AvoidedNumberColor = FLinearColor(0.75f, 0.75f, 0.75f, 1.f);

	UPROPERTY(Config, EditAnywhere, Category="Numbers", meta=(ClampMin="1"))
	int32 MaxActiveNumbers = 48;

	UPROPERTY(Config, EditAnywhere, Category="Health Bars", meta=(ClampMin="0.0"))
	float MonsterBarSeconds = 4.f;

	UPROPERTY(Config, EditAnywhere, Category="Health Bars", meta=(ClampMin="0.0"))
	float KilledMonsterBarSeconds = 0.4f;

	UPROPERTY(Config, EditAnywhere, Category="Health Bars", meta=(ClampMin="0"))
	int32 MaxMonsterBars = 24;

	UPROPERTY(Config, EditAnywhere, Category="Screen")
	TSoftObjectPtr<UMaterialInterface> DamageVignetteMaterial;

	UPROPERTY(Config, EditAnywhere, Category="Screen")
	FName VignetteColorParameterName = TEXT("VignetteColor");

	UPROPERTY(Config, EditAnywhere, Category="Screen")
	FName VignetteIntensityParameterName = TEXT("VignetteIntensity");

	UPROPERTY(Config, EditAnywhere, Category="Screen")
	FLinearColor DamageVignetteColor = FLinearColor(1.f, 0.05f, 0.05f, 1.f);

	UPROPERTY(Config, EditAnywhere, Category="Screen", meta=(ClampMin="0.01", ClampMax="1.0"))
	float DamageVignetteFullRatio = 0.15f;

	UPROPERTY(Config, EditAnywhere, Category="Screen", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DamageVignetteMinStrength = 0.2f;

	UPROPERTY(Config, EditAnywhere, Category="Screen", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DamageVignetteMaxIntensity = 0.6f;

	UPROPERTY(Config, EditAnywhere, Category="Screen", meta=(ClampMin="0.0"))
	float DamageVignetteSeconds = 0.35f;

	UPROPERTY(Config, EditAnywhere, Category="Screen", meta=(ClampMin="0.0", ClampMax="1.0"))
	float StatusVignetteScale = 0.3f;

	UPROPERTY(Config, EditAnywhere, Category="Screen", meta=(ClampMin="0.0", ClampMax="1.0"))
	float FallbackDamageTintAlpha = 0.1f;

	UPROPERTY(Config, EditAnywhere, Category="Screen", meta=(ClampMin="0.0", ClampMax="1.0"))
	float LowHealthVignetteFraction = 0.25f;

	UPROPERTY(Config, EditAnywhere, Category="Screen", meta=(ClampMin="0.0", ClampMax="1.0"))
	float LowHealthVignetteMinIntensity = 0.25f;

	UPROPERTY(Config, EditAnywhere, Category="Screen", meta=(ClampMin="0.0", ClampMax="1.0"))
	float LowHealthVignetteMaxIntensity = 0.4f;

	UPROPERTY(Config, EditAnywhere, Category="Screen", meta=(ClampMin="0.1"))
	float LowHealthVignettePeriodSeconds = 1.2f;

	UPROPERTY(Config, EditAnywhere, Category="Screen", meta=(ClampMin="0.0"))
	float FallbackRespawnDelaySeconds = 3.f;

	UPROPERTY(Config, EditAnywhere, Category="Budget", meta=(ClampMin="0"))
	int32 MaxNewNumbersPerFrame = 12;

	UPROPERTY(Config, EditAnywhere, Category="Budget", meta=(ClampMin="0"))
	int32 MaxNewEffectsPerFrame = 16;

	UPROPERTY(Config, EditAnywhere, Category="Budget", meta=(ClampMin="0"))
	int32 MaxOverlayChangesPerFrame = 8;

	UPROPERTY(Config, EditAnywhere, Category="Budget", meta=(ClampMin="0"))
	int32 MaxNewSoundsPerFrame = 6;

	UPROPERTY(Config, EditAnywhere, Category="Budget", meta=(ClampMin="0.0"))
	float MaxDeferredFeedbackSeconds = 0.1f;
};
