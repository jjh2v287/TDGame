#include "Combat/GAS/TDCombatGameplayEffects.h"
#include "Combat/GAS/TDCombatAttributeSet.h"
#include "Core/TDGameplayTags.h"
#include "GameplayEffectComponents/AssetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

UTDInstantDamageEffect::UTDInstantDamageEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FSetByCallerFloat DamageMagnitude;
	DamageMagnitude.DataTag = TDGameplayTags::Data_Damage;
	FGameplayModifierInfo& DamageModifier = Modifiers.AddDefaulted_GetRef();
	DamageModifier.Attribute = UTDCombatAttributeSet::GetHealthAttribute();
	DamageModifier.ModifierOp = EGameplayModOp::Additive;
	DamageModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(DamageMagnitude);

	UAssetTagsGameplayEffectComponent* AssetTags = CreateDefaultSubobject<UAssetTagsGameplayEffectComponent>(TEXT("DamageAssetTags"));
	GEComponents.Add(AssetTags);
	FInheritedTagContainer Tags;
	Tags.AddTag(TDGameplayTags::Effect_Damage);
	AssetTags->SetAndApplyAssetTagChanges(Tags);
}

UTDDurationStatusEffect::UTDDurationStatusEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(1.f));
	UAssetTagsGameplayEffectComponent* AssetTags = CreateDefaultSubobject<UAssetTagsGameplayEffectComponent>(TEXT("StatusAssetTags"));
	GEComponents.Add(AssetTags);
	FInheritedTagContainer Tags;
	Tags.AddTag(TDGameplayTags::Effect_Status);
	AssetTags->SetAndApplyAssetTagChanges(Tags);
}

UTDFreezeStatusEffect::UTDFreezeStatusEffect()
{
	UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("FrozenTargetTags"));
	GEComponents.Add(TargetTags);
	FInheritedTagContainer Tags;
	Tags.AddTag(TDGameplayTags::State_Frozen);
	TargetTags->SetAndApplyTargetTagChanges(Tags);
}

UTDDamageCooldownEffect::UTDDamageCooldownEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(1.f));
	UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("CooldownTargetTags"));
	GEComponents.Add(TargetTags);
	FInheritedTagContainer Tags;
	Tags.AddTag(TDGameplayTags::Effect_Cooldown_Damage);
	TargetTags->SetAndApplyTargetTagChanges(Tags);
}

UTDActionCooldownEffect::UTDActionCooldownEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	FSetByCallerFloat CooldownDuration;
	CooldownDuration.DataTag = TDGameplayTags::Data_Cooldown_Duration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(CooldownDuration);
}

UTDDeadEffect::UTDDeadEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	UTargetTagsGameplayEffectComponent* TargetTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("DeadTargetTags"));
	GEComponents.Add(TargetTags);
	FInheritedTagContainer Tags;
	Tags.AddTag(TDGameplayTags::State_Dead);
	TargetTags->SetAndApplyTargetTagChanges(Tags);
}
