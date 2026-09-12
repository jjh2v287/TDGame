#include "Combat/Damage/TDDamageSubsystem.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDDamageEntity.h"
#include "Combat/Damage/TDStatusDefinition.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TDGame.h"

ATDDamageEntity* UTDDamageSubsystem::Cast(UTDDamageDefinition* Definition, AActor* Caster, const FVector& Origin, const FVector& Target)
{
	if (!IsValid(Caster) || !IsValid(Definition) || Origin.ContainsNaN() || Target.ContainsNaN())
	{
		return nullptr;
	}

	const UTDCombatComponent* Combatant = Caster->FindComponentByClass<UTDCombatComponent>();
	if (!Combatant || !Combatant->IsAlive() || Combatant->IsFrozen())
	{
		return nullptr;
	}

	FTDDamageContext Context;
	Context.Caster = Caster;
	Context.Stats = Combatant->GetStats();
	Context.CastTarget = Target;
	Context.Direction = (Target - Origin).GetSafeNormal();
	if (Context.Direction.IsNearlyZero())
	{
		Context.Direction = Caster->GetActorForwardVector();
	}
	Context.Budget = MakeShared<FTDDamageChainBudget>();
	return SpawnEntity(Definition, Context, Origin);
}

ATDDamageEntity* UTDDamageSubsystem::SpawnEntity(UTDDamageDefinition* Definition, const FTDDamageContext& Context, const FVector& Location)
{
	if (!IsValid(Definition) || !GetWorld() || Location.ContainsNaN() || Context.Direction.ContainsNaN()
		|| !Context.Budget || Context.Depth < 0 || Context.Depth > 32 || Context.Budget->RemainingSpawns <= 0)
	{
		return nullptr;
	}

	FString ValidationError;
	if (!Definition->ValidateDefinition(ValidationError))
	{
		UE_LOG(LogTDGame, Warning, TEXT("Cannot spawn damage definition %s: %s"), *GetNameSafe(Definition), *ValidationError);
		return nullptr;
	}

	--Context.Budget->RemainingSpawns;
	const FVector Direction = Context.Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const FTransform Transform(Direction.Rotation(), Location);
	ATDDamageEntity* Entity = GetWorld()->SpawnActorDeferred<ATDDamageEntity>(ATDDamageEntity::StaticClass(), Transform,
		Context.Caster.Get(), ::Cast<APawn>(Context.Caster.Get()), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Entity)
	{
		return nullptr;
	}

	Entity->Initialize(Definition, Context);
	Entity->FinishSpawning(Transform);
	return Entity;
}

void UTDDamageSubsystem::ExecuteRules(const TArray<FTDDamageRule>& Rules, ETDDamageEvent Event,
	const FTDDamageContext& Context, AActor* Target, const FVector& Location, ATDDamageEntity* SourceEntity)
{
	if (!GetWorld() || !Context.Budget || Context.Depth < 0 || Context.Depth > 32 || ExecutionDepth >= 32 || Location.ContainsNaN())
	{
		return;
	}

	TGuardValue<int32> DepthGuard(ExecutionDepth, ExecutionDepth + 1);
	const TArray<FTDDamageRule> EventRules = Rules;
	for (const FTDDamageRule& Rule : EventRules)
	{
		if (Rule.Event != Event)
		{
			continue;
		}
		for (const FTDDamageAction& Action : Rule.Actions)
		{
			if (SourceEntity && (!IsValid(SourceEntity) || SourceEntity->IsActorBeingDestroyed()))
			{
				return;
			}
			if (Context.Budget->RemainingActions <= 0)
			{
				return;
			}
			--Context.Budget->RemainingActions;
			if (!FMath::IsFinite(Action.DelaySeconds) || Action.DelaySeconds < 0.f)
			{
				continue;
			}
			if (Action.DelaySeconds > 0.f)
			{
				if (IsValid(SourceEntity) && Event != ETDDamageEvent::End && Event != ETDDamageEvent::Expire)
				{
					SourceEntity->ScheduleAction(Action, EventRules, Event, Context, Target, Location);
				}
				continue;
			}
			ExecuteAction(Action, EventRules, Event, Context, Target, Location, SourceEntity);
		}
	}
}

void UTDDamageSubsystem::ExecuteScheduledAction(const FTDDamageAction& Action, const TArray<FTDDamageRule>& Rules,
	ETDDamageEvent Event, const FTDDamageContext& Context, AActor* Target, const FVector& Location, ATDDamageEntity* SourceEntity)
{
	if (!GetWorld() || !IsValid(SourceEntity) || SourceEntity->IsActorBeingDestroyed() || !Context.Budget
		|| Context.Depth < 0 || Context.Depth > 32 || ExecutionDepth >= 32 || Location.ContainsNaN())
	{
		return;
	}
	TGuardValue<int32> DepthGuard(ExecutionDepth, ExecutionDepth + 1);
	ExecuteAction(Action, Rules, Event, Context, Target, Location, SourceEntity);
}

void UTDDamageSubsystem::ExecuteAction(const FTDDamageAction& Action, const TArray<FTDDamageRule>& Rules,
	ETDDamageEvent Event, const FTDDamageContext& Context, AActor* Target, const FVector& Location, ATDDamageEntity* SourceEntity)
{
	if (Action.Type == ETDDamageActionType::ApplyHoming || Action.Type == ETDDamageActionType::StopHoming)
	{
		if (!IsValid(SourceEntity) || SourceEntity->IsActorBeingDestroyed())
		{
			return;
		}
		if (Action.Type == ETDDamageActionType::ApplyHoming)
		{
			SourceEntity->ApplyHoming(Action.Homing, Target);
		}
		else
		{
			SourceEntity->StopHoming();
		}
		return;
	}
	UTDCombatComponent* Combatant = IsValid(Target) ? Target->FindComponentByClass<UTDCombatComponent>() : nullptr;
	if (Action.Type == ETDDamageActionType::Damage)
	{
		if (!Combatant || !Combatant->IsAlive())
		{
			return;
		}

		const FTDDamageResult Result = Combatant->ReceiveDamage(Action.Magnitude.Evaluate(Context.Stats), Action.Element, Action.bCanCrit, Context);
		if (SourceEntity && (!IsValid(SourceEntity) || SourceEntity->IsActorBeingDestroyed()))
		{
			return;
		}
		if (Result.AppliedDamage > 0.f && IsValid(Combatant) && Combatant->IsAlive() && IsValid(Action.Status))
		{
			Combatant->ApplyStatus(Action.Status, Context, Result.AppliedDamage);
		}
		if (Result.bWasKilled && Event != ETDDamageEvent::Kill)
		{
			FTDDamageContext KillContext = Context;
			++KillContext.Depth;
			ExecuteRules(Rules, ETDDamageEvent::Kill, KillContext, Target, Location, SourceEntity);
		}
		return;
	}

	if (Action.Type == ETDDamageActionType::ApplyStatus)
	{
		if (Combatant && Combatant->IsAlive() && IsValid(Action.Status))
		{
			Combatant->ApplyStatus(Action.Status, Context, Action.Magnitude.Evaluate(Context.Stats));
		}
		return;
	}

	if (Action.Type != ETDDamageActionType::SpawnEntity || !IsValid(Action.Entity)
		|| Action.SpawnOffset.ContainsNaN() || !FMath::IsFinite(Action.ScatterRadius))
	{
		return;
	}

	FVector Anchor = Location;
	if (Action.SpawnAnchor == ETDDamageSpawnAnchor::Target && IsValid(Target))
	{
		Anchor = Target->GetActorLocation();
	}
	if (Action.SpawnAnchor == ETDDamageSpawnAnchor::CastTarget)
	{
		Anchor = Context.CastTarget;
	}

	const int32 SpawnCount = FMath::Clamp(Action.SpawnCount, 1, 32);
	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		if (SourceEntity && (!IsValid(SourceEntity) || SourceEntity->IsActorBeingDestroyed()))
		{
			return;
		}
		if (Context.Budget->RemainingSpawns <= 0)
		{
			return;
		}
		const float Angle = FMath::FRand() * UE_TWO_PI;
		const float Distance = FMath::Sqrt(FMath::FRand()) * FMath::Max(0.f, Action.ScatterRadius);
		const FVector SpawnLocation = Anchor + Action.SpawnOffset + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.f);
		FTDDamageContext ChildContext = Context;
		++ChildContext.Depth;
		if (Action.SpawnDirection == ETDDamageDirection::Down)
		{
			ChildContext.Direction = FVector::DownVector;
		}
		if (Action.SpawnDirection == ETDDamageDirection::TowardCastTarget)
		{
			ChildContext.Direction = (Context.CastTarget - SpawnLocation).GetSafeNormal(UE_SMALL_NUMBER, Context.Direction);
		}
		SpawnEntity(Action.Entity, ChildContext, SpawnLocation);
	}
}

void UTDDamageSubsystem::RegisterCombatant(UTDCombatComponent* Combatant)
{
	if (IsValid(Combatant))
	{
		Combatants.Add(Combatant);
	}
}

void UTDDamageSubsystem::UnregisterCombatant(UTDCombatComponent* Combatant)
{
	Combatants.Remove(Combatant);
}

bool UTDDamageSubsystem::CanTarget(const UTDCombatComponent* Target, const FTDDamageContext& Context, ETDDamageTargetPolicy Policy) const
{
	if (!IsValid(Target) || !Target->IsAlive() || !IsValid(Target->GetOwner())
		|| Target->GetOwner()->IsActorBeingDestroyed() || Target->GetOwner() == Context.Caster.Get())
	{
		return false;
	}

	const bool bIsAlly = Target->GetStats().TeamId == Context.Stats.TeamId;
	return Policy == ETDDamageTargetPolicy::Everyone
		|| (Policy == ETDDamageTargetPolicy::Enemies && !bIsAlly)
		|| (Policy == ETDDamageTargetPolicy::Allies && bIsAlly);
}

void UTDDamageSubsystem::GatherTargets(const FVector& Center, float Radius, float HalfHeight,
	const FTDDamageContext& Context, ETDDamageTargetPolicy Policy, TArray<UTDCombatComponent*>& OutTargets) const
{
	OutTargets.Reset();
	if (Center.ContainsNaN() || !FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius < 0.f || HalfHeight < 0.f)
	{
		return;
	}

	const double RadiusSquared = FMath::Square(static_cast<double>(Radius));
	for (const TWeakObjectPtr<UTDCombatComponent>& Entry : Combatants)
	{
		UTDCombatComponent* Combatant = Entry.Get();
		if (!CanTarget(Combatant, Context, Policy))
		{
			continue;
		}
		const FVector Offset = Combatant->GetOwner()->GetActorLocation() - Center;
		if (FMath::Abs(Offset.Z) <= HalfHeight && Offset.SizeSquared2D() <= RadiusSquared)
		{
			OutTargets.Add(Combatant);
		}
	}
}

void UTDDamageSubsystem::Deinitialize()
{
	Combatants.Empty();
	Super::Deinitialize();
}
