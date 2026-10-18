#include "Combat/TDCombatLibrary.h"
#include "AbilitySystemGlobals.h"
#include "Combat/TDCombatComponent.h"
#include "GameFramework/Actor.h"

float UTDCombatLibrary::TryApplyDamage(AActor* TargetActor, const FTDDamageSpec& DamageSpec)
{
	if (!TargetActor || !FMath::IsFinite(DamageSpec.Amount) || DamageSpec.Amount <= 0.f)
	{
		return 0.f;
	}

	AActor* Instigator = DamageSpec.Instigator.Get();
	if (Instigator == TargetActor)
	{
		return 0.f;
	}
	if (Instigator && !AreActorsHostile(Instigator, TargetActor))
	{
		return 0.f;
	}

	UTDCombatComponent* TargetCombatant = GetCombatComponent(TargetActor);
	if (!TargetCombatant || !TargetCombatant->IsAlive())
	{
		return 0.f;
	}

	const FTDDamageContext Context = MakeDamageContext(Instigator, TargetActor->GetActorLocation());
	const FTDDamageResult Result = TargetCombatant->ReceiveDamage(DamageSpec.Amount, DamageSpec.Element, DamageSpec.bCanCrit, Context);
	return FMath::Max(0.f, Result.AppliedDamage);
}

bool UTDCombatLibrary::AreActorsHostile(const AActor* SourceActor, const AActor* TargetActor)
{
	if (!SourceActor || !TargetActor || SourceActor == TargetActor)
	{
		return false;
	}
	const UTDCombatComponent* SourceCombatant = GetCombatComponent(SourceActor);
	const UTDCombatComponent* TargetCombatant = GetCombatComponent(TargetActor);
	if (!SourceCombatant || !TargetCombatant)
	{
		return false;
	}
	return SourceCombatant->GetStats().TeamId != TargetCombatant->GetStats().TeamId;
}

bool UTDCombatLibrary::IsActorAlive(const AActor* Actor)
{
	const UTDCombatComponent* Combatant = GetCombatComponent(Actor);
	return Combatant && Combatant->IsAlive();
}

int32 UTDCombatLibrary::GetActorTeamId(const AActor* Actor)
{
	const UTDCombatComponent* Combatant = GetCombatComponent(Actor);
	return Combatant ? Combatant->GetStats().TeamId : INDEX_NONE;
}

UTDCombatComponent* UTDCombatLibrary::GetCombatComponent(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	return Cast<UTDCombatComponent>(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(const_cast<AActor*>(Actor)));
}

FTDDamageContext UTDCombatLibrary::MakeDamageContext(AActor* Instigator, const FVector& CastTarget)
{
	FTDDamageContext Context;
	Context.Caster = Instigator;
	Context.CastTarget = CastTarget;
	Context.Budget = MakeShared<FTDDamageChainBudget>();
	if (!Instigator)
	{
		return Context;
	}
	if (const UTDCombatComponent* InstigatorCombatant = GetCombatComponent(Instigator))
	{
		Context.Stats = InstigatorCombatant->GetStats();
	}
	Context.Direction = Instigator->GetActorForwardVector();
	return Context;
}
