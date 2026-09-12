#include "Combat/GAS/TDDamageGameplayAbility.h"
#include "Combat/GAS/TDCombatGameplayEffects.h"
#include "Core/TDGameplayTags.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageSubsystem.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"
#include "GameplayEffect.h"

UTDDamageGameplayAbility::UTDDamageGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
	SetAssetTags(FGameplayTagContainer(TDGameplayTags::Ability_DamageCast));
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(TDGameplayTags::State_Frozen);
}

bool UTDDamageGameplayAbility::DidLastCastSucceed() const
{
	return bLastCastSucceeded;
}

bool UTDDamageGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid() || ActorInfo->AvatarActor->IsActorBeingDestroyed())
	{
		return false;
	}
	const UTDCombatComponent* Combatant = Cast<UTDCombatComponent>(ActorInfo->AbilitySystemComponent.Get());
	const UTDDamageDefinition* Definition = Cast<UTDDamageDefinition>(GetSourceObject(Handle, ActorInfo));
	FString Error;
	if (!IsValid(Combatant) || !Combatant->IsAlive() || Combatant->IsFrozen()
		|| !IsValid(Definition) || !Definition->ValidateDefinition(Error))
	{
		return false;
	}
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

bool UTDDamageGameplayAbility::CheckCooldown(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
	{
		return false;
	}
	const UTDDamageDefinition* Definition = Cast<UTDDamageDefinition>(GetSourceObject(Handle, ActorInfo));
	if (!IsValid(Definition))
	{
		return false;
	}
	FGameplayEffectQuery Query;
	Query.CustomMatchDelegate.BindLambda([Definition](const FActiveGameplayEffect& Effect)
	{
		return IsValid(Effect.Spec.Def) && Effect.Spec.Def->IsA<UTDDamageCooldownEffect>()
			&& Effect.Spec.GetContext().GetSourceObject() == Definition;
	});
	return ActorInfo->AbilitySystemComponent->GetActiveEffects(Query).IsEmpty();
}

void UTDDamageGameplayAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	CastCooldownHandle.Invalidate();
	if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
	{
		return;
	}
	const UTDDamageDefinition* Definition = Cast<UTDDamageDefinition>(GetSourceObject(Handle, ActorInfo));
	if (!IsValid(Definition) || Definition->Cooldown <= 0.f)
	{
		return;
	}
	const FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(Handle, ActorInfo, ActivationInfo,
		UTDDamageCooldownEffect::StaticClass(), GetAbilityLevel(Handle, ActorInfo));
	if (!Spec.IsValid())
	{
		return;
	}
	Spec.Data->SetDuration(Definition->Cooldown, true);
	Spec.Data->GetContext().AddSourceObject(Definition);
	CastCooldownHandle = ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
}

void UTDDamageGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bLastCastSucceeded = false;
	CastCooldownHandle.Invalidate();
	if (!CanActivateAbility(Handle, ActorInfo) || !TriggerEventData)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
		return;
	}
	const FGameplayAbilityTargetData* TargetData = TriggerEventData->TargetData.Get(0);
	const FHitResult* Hit = TargetData ? TargetData->GetHitResult() : nullptr;
	if (!Hit || Hit->ImpactPoint.ContainsNaN())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
		return;
	}
	UTDDamageDefinition* Definition = Cast<UTDDamageDefinition>(GetSourceObject(Handle, ActorInfo));
	AActor* Avatar = ActorInfo->AvatarActor.Get();
	UTDDamageSubsystem* Subsystem = Avatar->GetWorld() ? Avatar->GetWorld()->GetSubsystem<UTDDamageSubsystem>() : nullptr;
	if (!Subsystem)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
		return;
	}
	const FVector CasterLocation = Avatar->GetActorLocation();
	FVector CastTarget = CasterLocation + (Hit->ImpactPoint - CasterLocation).GetClampedToMaxSize(Definition->CastRange);
	FVector Origin = CastTarget;
	if (Definition->Mode == ETDDamageEntityMode::Projectile)
	{
		CastTarget.Z = CasterLocation.Z;
		const FVector ForwardDirection = Avatar->GetActorForwardVector().GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
		const FVector Direction = (CastTarget - CasterLocation).GetSafeNormal2D(UE_SMALL_NUMBER, ForwardDirection);
		const double TargetDistance = FVector::Dist2D(CasterLocation, CastTarget);
		Origin = CasterLocation + Direction * FMath::Min(60., TargetDistance * 0.5);
		if (TargetDistance < UE_SMALL_NUMBER)
		{
			CastTarget = CasterLocation + Direction * FMath::Min(100.f, Definition->CastRange);
		}
	}
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
		return;
	}
	const UTDCombatComponent* Combatant = Cast<UTDCombatComponent>(ActorInfo->AbilitySystemComponent.Get());
	const bool bHasCommittedCooldown = Definition->Cooldown <= 0.f || CastCooldownHandle.IsValid()
		|| UAbilitySystemGlobals::Get().ShouldIgnoreCooldowns();
	if (IsActive() && bHasCommittedCooldown && IsValid(Subsystem) && IsValid(Avatar)
		&& !Avatar->IsActorBeingDestroyed() && IsValid(Combatant) && Combatant->IsAlive() && !Combatant->IsFrozen())
	{
		bLastCastSucceeded = Subsystem->Cast(Definition, Avatar, Origin, CastTarget) != nullptr;
	}
	if (!bLastCastSucceeded && CastCooldownHandle.IsValid() && ActorInfo->AbilitySystemComponent.IsValid())
	{
		ActorInfo->AbilitySystemComponent->RemoveActiveGameplayEffect(CastCooldownHandle);
		CastCooldownHandle.Invalidate();
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, false, !bLastCastSucceeded);
}
