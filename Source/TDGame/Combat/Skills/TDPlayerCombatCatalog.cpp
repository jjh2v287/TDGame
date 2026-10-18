#include "Combat/Skills/TDPlayerCombatCatalog.h"
#include "Animation/AnimSequence.h"
#include "Combat/Skills/TDCombatActionTypes.h"
#include "Combat/Skills/TDCombatStyleDefinition.h"
#include "Core/TDGameplayTags.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace TDPlayerCombatCatalogDetail
{
	constexpr float SourceFramesPerSecond = 60.f;
	constexpr float HitWindowLeadSeconds = 0.03f;
	constexpr float HitWindowTrailSeconds = 0.05f;
	constexpr float SwordSweepRadius = 18.f;
	constexpr int32 SwordBladeSampleCount = 6;
	constexpr float SwordAttackRatio = 1.f;

	struct FTDSwordComboStage
	{
		FGameplayTag ActionTag;
		const TCHAR* AnimationPath = nullptr;
		int32 LastFrame = 1;
		int32 SwingStartFrame = 1;
		int32 SwingEndFrame = 1;
		int32 BranchFrame = 1;
		float BaseDamage = 0.f;
	};

	const FName& GetSwordGripSocketName()
	{
		static const FName SocketName(TEXT("HandGrip_R"));
		return SocketName;
	}

	TArray<FTDSwordComboStage> MakeSwordComboStages()
	{
		return
		{
			{ TDGameplayTags::Action_Attack_Primary_01, TEXT("/Game/Characters/Mannequins/Anims/Sword/AS_TD_Player_SwordCombo01.AS_TD_Player_SwordCombo01"), 86, 17, 22, 36, 14.f },
			{ TDGameplayTags::Action_Attack_Primary_02, TEXT("/Game/Characters/Mannequins/Anims/Sword/AS_TD_Player_SwordCombo02.AS_TD_Player_SwordCombo02"), 86, 16, 24, 36, 14.f },
			{ TDGameplayTags::Action_Attack_Primary_03, TEXT("/Game/Characters/Mannequins/Anims/Sword/AS_TD_Player_SwordCombo03.AS_TD_Player_SwordCombo03"), 92, 19, 21, 38, 14.f },
			{ TDGameplayTags::Action_Attack_Primary_04, TEXT("/Game/Characters/Mannequins/Anims/Sword/AS_TD_Player_SwordCombo04.AS_TD_Player_SwordCombo04"), 68, 17, 37, 68, 22.f }
		};
	}

	const TCHAR* const LightHitReactionPath = TEXT("/Game/Characters/Mannequins/Anims/Reaction/AS_TD_Player_HitReact_Light.AS_TD_Player_HitReact_Light");
	const TCHAR* const HeavyHitReactionPath = TEXT("/Game/Characters/Mannequins/Anims/Reaction/AS_TD_Player_HitReact_Heavy.AS_TD_Player_HitReact_Heavy");

	float FrameToSeconds(const int32 Frame)
	{
		return static_cast<float>(Frame - 1) / SourceFramesPerSecond;
	}

	bool DoesAnimationPackageExist(const TCHAR* AnimationPath)
	{
		return FPackageName::DoesPackageExist(FSoftObjectPath(AnimationPath).GetLongPackageName());
	}

	FTDMeleeSweepSettings MakeSwordSweep(const float BaseDamage)
	{
		FTDMeleeSweepSettings Sweep;
		Sweep.WeaponBaseSocketName = GetSwordGripSocketName();
		Sweep.WeaponTipSocketName = GetSwordGripSocketName();
		Sweep.BaseLocalOffset = FVector(-1.15f, 3.80f, 6.06f);
		Sweep.TipLocalOffset = FVector(-13.37f, 28.72f, 98.11f);
		Sweep.BladeSampleCount = SwordBladeSampleCount;
		Sweep.SweepRadius = SwordSweepRadius;
		Sweep.bLockHeightToOwner = false;
		Sweep.TargetPolicy = ETDDamageTargetPolicy::Enemies;

		FTDDamageAction DamageAction;
		DamageAction.Type = ETDDamageActionType::Damage;
		DamageAction.Magnitude.Base = BaseDamage;
		DamageAction.Magnitude.AttackRatio = SwordAttackRatio;
		DamageAction.Element = ETDDamageElement::Physical;
		DamageAction.bCanCrit = true;

		FTDDamageRule& HitRule = Sweep.HitRules.AddDefaulted_GetRef();
		HitRule.Event = ETDDamageEvent::Hit;
		HitRule.Actions.Add(DamageAction);
		return Sweep;
	}

	FTDCombatActionDefinition MakeSwordComboAction(const FTDSwordComboStage& Stage)
	{
		const float ClipEndSeconds = FrameToSeconds(Stage.LastFrame);
		const float SwingStartSeconds = FrameToSeconds(Stage.SwingStartFrame);
		const float SwingEndSeconds = FrameToSeconds(Stage.SwingEndFrame);
		const float BranchSeconds = FrameToSeconds(Stage.BranchFrame);

		FTDCombatActionDefinition Definition;
		Definition.DamageElement = ETDDamageElement::Physical;
		Definition.HitExecutionType = ETDCombatHitExecutionType::TimelineSweep;
		FTDActionAnimation& Action = Definition.Action;
		Action.Animation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(Stage.AnimationPath));
		Action.bUseRootMotion = true;

		FTDMeleeHitWindow& HitWindow = Action.HitWindows.AddDefaulted_GetRef();
		HitWindow.Window.StartSeconds = FMath::Max(SwingStartSeconds - HitWindowLeadSeconds, 0.f);
		HitWindow.Window.EndSeconds = FMath::Min(SwingEndSeconds + HitWindowTrailSeconds, ClipEndSeconds);
		HitWindow.Sweep = MakeSwordSweep(Stage.BaseDamage);

		if (BranchSeconds < ClipEndSeconds)
		{
			FTDTagWindow& ComboWindow = Action.TagWindows.AddDefaulted_GetRef();
			ComboWindow.Window.StartSeconds = BranchSeconds;
			ComboWindow.Window.EndSeconds = ClipEndSeconds;
			ComboWindow.Tags.AddTag(TDGameplayTags::State_ComboWindow);
		}

		Action.InputBufferWindow.StartSeconds = SwingEndSeconds;
		Action.InputBufferWindow.EndSeconds = ClipEndSeconds;
		return Definition;
	}

	FTDCombatReactionDefinition MakeHitReaction(const TCHAR* AnimationPath)
	{
		FTDCombatReactionDefinition Reaction;
		Reaction.Action.Animation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(AnimationPath));
		Reaction.Action.bUseRootMotion = false;
		return Reaction;
	}

	template <typename ValueType>
	TMap<FGameplayTag, ValueType>* FindStyleMap(UTDCombatStyleDefinition& Style, const TCHAR* PropertyName)
	{
		const FMapProperty* MapProperty = FindFProperty<FMapProperty>(UTDCombatStyleDefinition::StaticClass(), PropertyName);
		const FStructProperty* ValueProperty = MapProperty ? CastField<FStructProperty>(MapProperty->GetValueProperty()) : nullptr;
		if (!ValueProperty || ValueProperty->Struct != ValueType::StaticStruct())
		{
			return nullptr;
		}
		return MapProperty->ContainerPtrToValuePtr<TMap<FGameplayTag, ValueType>>(&Style);
	}
}

UTDCombatStyleDefinition* TDPlayerCombatCatalog::CreateSwordShieldStyle(UObject* Outer)
{
	using namespace TDPlayerCombatCatalogDetail;

	UTDCombatStyleDefinition* Style = NewObject<UTDCombatStyleDefinition>(Outer ? Outer : GetTransientPackage(), NAME_None, RF_Transient);
	TMap<FGameplayTag, FTDCombatActionDefinition>* ActionMap = FindStyleMap<FTDCombatActionDefinition>(*Style, TEXT("ActionMap"));
	TMap<FGameplayTag, FTDCombatReactionDefinition>* ReactionMap = FindStyleMap<FTDCombatReactionDefinition>(*Style, TEXT("ReactionMap"));
	if (!ActionMap || !ReactionMap)
	{
		return nullptr;
	}

	for (const FTDSwordComboStage& Stage : MakeSwordComboStages())
	{
		if (!DoesAnimationPackageExist(Stage.AnimationPath))
		{
			continue;
		}
		ActionMap->Add(Stage.ActionTag, MakeSwordComboAction(Stage));
	}

	if (DoesAnimationPackageExist(LightHitReactionPath))
	{
		ReactionMap->Add(TDGameplayTags::Action_Reaction_Hit, MakeHitReaction(LightHitReactionPath));
	}
	if (DoesAnimationPackageExist(HeavyHitReactionPath))
	{
		ReactionMap->Add(TDGameplayTags::Action_Reaction_HeavyHit, MakeHitReaction(HeavyHitReactionPath));
	}
	return Style;
}
