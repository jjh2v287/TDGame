#include "Combat/TDCombatComponent.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Combat/GAS/TDCombatAttributeSet.h"
#include "Combat/GAS/TDCombatGameplayEffects.h"
#include "Combat/GAS/TDDamageGameplayAbility.h"
#include "Core/TDGameplayTags.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageSubsystem.h"
#include "Engine/World.h"
#include "GameplayEffectExtension.h"
#include "TDGame.h"

namespace TDCombatInitialization
{
	float NonNegative(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.f, Value) : 0.f;
	}

	bool CanApplyEffect(const FActiveGameplayEffectsContainer&, const FGameplayEffectSpec& Spec)
	{
		if (!Spec.Def)
		{
			return false;
		}
		if (Spec.GetDuration() == 0.f || Spec.GetPeriod() > 0.f)
		{
			return true;
		}
		for (const FGameplayModifierInfo& Modifier : Spec.Def->Modifiers)
		{
			if (Modifier.Attribute == UTDCombatAttributeSet::GetHealthAttribute())
			{
				UE_LOG(LogTDGame, Warning, TEXT("Rejected persistent Health modifier in %s. Use MaxHealth for buffs and instant or periodic effects for Health."), *GetNameSafe(Spec.Def));
				return false;
			}
		}
		return true;
	}
}

UTDCombatComponent::UTDCombatComponent()
{
	SetIsReplicatedByDefault(false);
	GameplayEffectApplicationQueries.Add(FGameplayEffectApplicationQuery::CreateStatic(&TDCombatInitialization::CanApplyEffect));
}

void UTDCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	InitAbilityActorInfo(GetOwner(), GetOwner());
	CombatAttributes = const_cast<UTDCombatAttributeSet*>(GetSet<UTDCombatAttributeSet>());
	if (!CombatAttributes)
	{
		CombatAttributes = NewObject<UTDCombatAttributeSet>(GetOwner());
		AddAttributeSetSubobject(CombatAttributes.Get());
	}
	HealthChangedHandle = GetGameplayAttributeValueChangeDelegate(UTDCombatAttributeSet::GetHealthAttribute()).AddUObject(this, &UTDCombatComponent::HandleHealthChanged);
	FrozenTagChangedHandle = RegisterGameplayTagEvent(TDGameplayTags::State_Frozen, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &UTDCombatComponent::HandleFrozenTagChanged);
	SetStats(Stats);
	if (UTDDamageSubsystem* Subsystem = GetWorld()->GetSubsystem<UTDDamageSubsystem>())
	{
		Subsystem->RegisterCombatant(this);
	}
}

void UTDCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bIsEndingPlay = true;
	ClearStatuses();
	CancelAllAbilities();
	GetGameplayAttributeValueChangeDelegate(UTDCombatAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
	RegisterGameplayTagEvent(TDGameplayTags::State_Frozen, EGameplayTagEventType::NewOrRemoved).Remove(FrozenTagChangedHandle);
	if (UWorld* World = GetWorld())
	{
		if (UTDDamageSubsystem* Subsystem = World->GetSubsystem<UTDDamageSubsystem>())
		{
			Subsystem->UnregisterCombatant(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

FTDCombatStats UTDCombatComponent::GetStats() const
{
	FTDCombatStats Snapshot = Stats;
	if (!CombatAttributes)
	{
		return Snapshot;
	}
	Snapshot.Level = FMath::Clamp(FMath::RoundToInt(CombatAttributes->GetLevel()), 1, 1000);
	Snapshot.BaseMaxHealth = CombatAttributes->GetMaxHealth();
	Snapshot.HealthPerLevel = 0.f;
	Snapshot.MaxStamina = CombatAttributes->GetMaxStamina();
	Snapshot.AttackPower = CombatAttributes->GetAttackPower();
	Snapshot.SpellPower = CombatAttributes->GetSpellPower();
	Snapshot.AttackPowerPerLevel = 0.f;
	Snapshot.SpellPowerPerLevel = 0.f;
	Snapshot.Armor = CombatAttributes->GetArmor();
	Snapshot.MagicResistance = CombatAttributes->GetMagicResistance();
	Snapshot.CriticalChance = CombatAttributes->GetCriticalChance();
	Snapshot.CriticalMultiplier = CombatAttributes->GetCriticalMultiplier();
	return Snapshot;
}

float UTDCombatComponent::GetCurrentHealth() const
{
	return CombatAttributes ? CombatAttributes->GetHealth() : Stats.GetMaxHealth();
}

float UTDCombatComponent::GetCurrentStamina() const
{
	return CombatAttributes ? CombatAttributes->GetStamina() : Stats.MaxStamina;
}

float UTDCombatComponent::GetMaxStamina() const
{
	return CombatAttributes ? CombatAttributes->GetMaxStamina() : Stats.MaxStamina;
}

bool UTDCombatComponent::ConsumeStamina(float Cost)
{
	if (!CombatAttributes || !FMath::IsFinite(Cost) || Cost < 0.f)
	{
		return false;
	}
	if (Cost == 0.f)
	{
		return true;
	}
	const float CurrentStamina = GetCurrentStamina();
	if (CurrentStamina < Cost)
	{
		return false;
	}
	SetNumericAttributeBase(UTDCombatAttributeSet::GetStaminaAttribute(), CurrentStamina - Cost);
	return true;
}

void UTDCombatComponent::RestoreStamina(float Amount)
{
	if (!CombatAttributes || !FMath::IsFinite(Amount) || Amount <= 0.f)
	{
		return;
	}
	SetNumericAttributeBase(UTDCombatAttributeSet::GetStaminaAttribute(), FMath::Min(GetCurrentStamina() + Amount, GetMaxStamina()));
}

bool UTDCombatComponent::IsAlive() const
{
	return GetCurrentHealth() > 0.f && !HasMatchingGameplayTag(TDGameplayTags::State_Dead);
}

bool UTDCombatComponent::IsFrozen() const
{
	return HasMatchingGameplayTag(TDGameplayTags::State_Frozen);
}

void UTDCombatComponent::SetStats(const FTDCombatStats& NewStats, bool bResetHealth)
{
	if (bIsEndingPlay || bIsApplyingInitialStats)
	{
		return;
	}
	Stats = NewStats;
	Stats.Level = FMath::Clamp(Stats.Level, 1, 1000);
	Stats.BaseMaxHealth = FMath::Max(1.f, TDCombatInitialization::NonNegative(Stats.BaseMaxHealth));
	Stats.HealthPerLevel = TDCombatInitialization::NonNegative(Stats.HealthPerLevel);
	Stats.MaxStamina = TDCombatInitialization::NonNegative(Stats.MaxStamina);
	Stats.AttackPower = TDCombatInitialization::NonNegative(Stats.AttackPower);
	Stats.SpellPower = TDCombatInitialization::NonNegative(Stats.SpellPower);
	Stats.AttackPowerPerLevel = TDCombatInitialization::NonNegative(Stats.AttackPowerPerLevel);
	Stats.SpellPowerPerLevel = TDCombatInitialization::NonNegative(Stats.SpellPowerPerLevel);
	Stats.Armor = TDCombatInitialization::NonNegative(Stats.Armor);
	Stats.MagicResistance = TDCombatInitialization::NonNegative(Stats.MagicResistance);
	Stats.CriticalChance = FMath::Clamp(TDCombatInitialization::NonNegative(Stats.CriticalChance), 0.f, 1.f);
	Stats.CriticalMultiplier = FMath::Max(1.f, TDCombatInitialization::NonNegative(Stats.CriticalMultiplier));
	if (!CombatAttributes)
	{
		return;
	}
	{
		TGuardValue<bool> InitializationGuard(bIsApplyingInitialStats, true);
		if (bResetHealth)
		{
			ClearStatuses();
			bDeathReported = false;
		}
		ApplyInitialAttributes(bResetHealth);
	}
	RefreshDeathState();
	UpdateFreezeState();
}

void UTDCombatComponent::SetCombatLevel(int32 NewLevel)
{
	FTDCombatStats UpdatedStats = Stats;
	UpdatedStats.Level = FMath::Clamp(NewLevel, 1, 1000);
	SetStats(UpdatedStats, false);
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		Spec.Level = UpdatedStats.Level;
		MarkAbilitySpecDirty(Spec);
	}
}

void UTDCombatComponent::ApplyInitialAttributes(bool bResetHealth)
{
	SetNumericAttributeBase(UTDCombatAttributeSet::GetMaxHealthAttribute(), Stats.GetMaxHealth());
	SetNumericAttributeBase(UTDCombatAttributeSet::GetLevelAttribute(), Stats.Level);
	SetNumericAttributeBase(UTDCombatAttributeSet::GetAttackPowerAttribute(), Stats.GetAttackPower());
	SetNumericAttributeBase(UTDCombatAttributeSet::GetSpellPowerAttribute(), Stats.GetSpellPower());
	SetNumericAttributeBase(UTDCombatAttributeSet::GetArmorAttribute(), Stats.Armor);
	SetNumericAttributeBase(UTDCombatAttributeSet::GetMagicResistanceAttribute(), Stats.MagicResistance);
	SetNumericAttributeBase(UTDCombatAttributeSet::GetCriticalChanceAttribute(), Stats.CriticalChance);
	SetNumericAttributeBase(UTDCombatAttributeSet::GetCriticalMultiplierAttribute(), Stats.CriticalMultiplier);
	SetNumericAttributeBase(UTDCombatAttributeSet::GetMaxStaminaAttribute(), Stats.MaxStamina);
	if (bResetHealth)
	{
		SetNumericAttributeBase(UTDCombatAttributeSet::GetHealthAttribute(), CombatAttributes->GetMaxHealth());
		SetNumericAttributeBase(UTDCombatAttributeSet::GetStaminaAttribute(), CombatAttributes->GetMaxStamina());
	}
}

void UTDCombatComponent::RefreshDeathState()
{
	if (GetCurrentHealth() > 0.f)
	{
		bDeathReported = false;
		const FActiveGameplayEffectHandle PreviousHandle = DeadEffectHandle;
		DeadEffectHandle.Invalidate();
		if (PreviousHandle.IsValid())
		{
			RemoveActiveGameplayEffect(PreviousHandle);
		}
		return;
	}
	if (!DeadEffectHandle.IsValid() && !bIsEndingPlay && !bIsApplyingDeadEffect)
	{
		TGuardValue<bool> ApplyingDeadEffectGuard(bIsApplyingDeadEffect, true);
		const FActiveGameplayEffectHandle AppliedHandle = ApplyGameplayEffectToSelf(GetDefault<UTDDeadEffect>(), GetStats().Level, MakeEffectContext());
		if (bIsEndingPlay || GetCurrentHealth() > 0.f)
		{
			if (AppliedHandle.IsValid())
			{
				RemoveActiveGameplayEffect(AppliedHandle);
			}
			return;
		}
		DeadEffectHandle = AppliedHandle;
	}
}

FTDDamageResult UTDCombatComponent::ReceiveDamage(float RawDamage, ETDDamageElement Element, bool bCanCrit, const FTDDamageContext& Context)
{
	FTDDamageResult Result;
	if (bIsEndingPlay || !CombatAttributes || !IsAlive() || !FMath::IsFinite(RawDamage) || RawDamage <= 0.f)
	{
		return Result;
	}
	const float Chance = FMath::Clamp(TDCombatInitialization::NonNegative(Context.Stats.CriticalChance), 0.f, 1.f);
	Result.bWasCritical = bCanCrit && Chance > 0.f && (Chance >= 1.f || FMath::FRand() < Chance);
	const double Multiplier = Result.bWasCritical ? FMath::Max(1.f, TDCombatInitialization::NonNegative(Context.Stats.CriticalMultiplier)) : 1.;
	const double Resistance = Element == ETDDamageElement::Physical ? CombatAttributes->GetArmor() : CombatAttributes->GetMagicResistance();
	const float Damage = static_cast<float>(FMath::Min(static_cast<double>(TNumericLimits<float>::Max()), RawDamage * Multiplier * 100. / (100. + Resistance)));
	FGameplayEffectContextHandle EffectContext = MakeEffectContext();
	EffectContext.AddInstigator(Context.Caster.Get(), Context.Caster.Get());
	FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(UTDInstantDamageEffect::StaticClass(), FMath::Clamp(Context.Stats.Level, 1, 1000), EffectContext);
	if (!Spec.IsValid())
	{
		return {};
	}
	Spec.Data->SetSetByCallerMagnitude(TDGameplayTags::Data_Damage, -Damage);
	FTDPendingCombatDamage Frame;
	Frame.CombatContext = Context;
	Frame.EffectContext = EffectContext;
	Frame.Result.bWasCritical = Result.bWasCritical;
	const int32 FrameIndex = PendingDamage.Add(MoveTemp(Frame));
	ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	Result = PendingDamage[FrameIndex].Result;
	PendingDamage.RemoveAt(FrameIndex);
	return Result;
}

void UTDCombatComponent::HandleHealthChanged(const FOnAttributeChangeData& Change)
{
	if (bIsEndingPlay || bIsApplyingInitialStats)
	{
		return;
	}
	if (Change.NewValue >= Change.OldValue)
	{
		RefreshDeathState();
		UpdateFreezeState();
		return;
	}
	const FGameplayEffectContextHandle EffectContext = Change.GEModData ? Change.GEModData->EffectSpec.GetContext() : FGameplayEffectContextHandle();
	int32 FrameIndex = INDEX_NONE;
	for (int32 Index = PendingDamage.Num() - 1; Index >= 0; --Index)
	{
		if (PendingDamage[Index].EffectContext == EffectContext)
		{
			FrameIndex = Index;
			break;
		}
	}
	FTDDamageContext Context;
	FTDDamageResult Result;
	Result.AppliedDamage = FMath::Max(0.f, Change.OldValue - Change.NewValue);
	if (FrameIndex != INDEX_NONE)
	{
		Context = PendingDamage[FrameIndex].CombatContext;
		Result.bWasCritical = PendingDamage[FrameIndex].Result.bWasCritical;
	}
	else
	{
		Context.Caster = EffectContext.GetOriginalInstigator();
		Context.Budget = MakeShared<FTDDamageChainBudget>();
		if (const UTDCombatComponent* Source = Cast<UTDCombatComponent>(EffectContext.GetOriginalInstigatorAbilitySystemComponent()))
		{
			Context.Stats = Source->GetStats();
		}
	}
	Result.bWasKilled = GetCurrentHealth() <= 0.f && !bDeathReported;
	const uint64 ExpectedGeneration = StatusGeneration + (Result.bWasKilled ? 1 : 0);
	if (Result.bWasKilled)
	{
		bDeathReported = true;
		ClearStatuses();
		RefreshDeathState();
		CancelAllAbilities();
	}
	Result.bWasKilled &= ExpectedGeneration == StatusGeneration && GetCurrentHealth() <= 0.f && !bIsEndingPlay;
	OnDamaged.Broadcast(Result, Context);
	Result.bWasKilled &= ExpectedGeneration == StatusGeneration && GetCurrentHealth() <= 0.f && !bIsEndingPlay
		&& IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed();
	if (FrameIndex != INDEX_NONE)
	{
		PendingDamage[FrameIndex].Result.AppliedDamage += Result.AppliedDamage;
		PendingDamage[FrameIndex].Result.bWasKilled = Result.bWasKilled;
	}
	if (Result.bWasKilled)
	{
		OnDeath.Broadcast(Context);
	}
}

bool UTDCombatComponent::TryCastDamageDefinition(UTDDamageDefinition* Definition, const FVector& Target)
{
	if (bIsEndingPlay || !CombatAttributes || !IsValid(Definition) || Target.ContainsNaN() || !IsAlive() || IsFrozen()
		|| !IsValid(GetOwner()) || !GetOwner()->HasAuthority() || !AbilityActorInfo.IsValid())
	{
		return false;
	}
	FString Error;
	if (!Definition->ValidateDefinition(Error))
	{
		return false;
	}
	FGameplayAbilitySpecHandle Handle;
	for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->IsA<UTDDamageGameplayAbility>() && Spec.SourceObject.Get() == Definition)
		{
			Handle = Spec.Handle;
			break;
		}
	}
	if (!Handle.IsValid())
	{
		GrantedDamageDefinitions.AddUnique(Definition);
		Handle = GiveAbility(FGameplayAbilitySpec(UTDDamageGameplayAbility::StaticClass(), GetStats().Level, INDEX_NONE, Definition));
	}
	FHitResult Hit;
	Hit.Location = Target;
	Hit.ImpactPoint = Target;
	FGameplayEventData Payload;
	Payload.EventTag = TDGameplayTags::Ability_DamageCast;
	Payload.Instigator = GetOwner();
	Payload.OptionalObject = Definition;
	Payload.TargetData.Add(new FGameplayAbilityTargetData_SingleTargetHit(Hit));
	if (!TriggerAbilityFromGameplayEvent(Handle, AbilityActorInfo.Get(), Payload.EventTag, &Payload, *this))
	{
		return false;
	}
	const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
	const UTDDamageGameplayAbility* Ability = Spec ? Cast<UTDDamageGameplayAbility>(Spec->GetPrimaryInstance()) : nullptr;
	return Ability && Ability->DidLastCastSucceed();
}

