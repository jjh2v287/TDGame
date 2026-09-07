#include "Combat/TDDamageDefinition.h"
#include "Combat/TDStatusDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	bool ValidateMinimum(const TCHAR* PropertyName, float Value, float Minimum, FString& OutError)
	{
		if (FMath::IsFinite(Value) && Value >= Minimum)
		{
			return true;
		}

		OutError = FString::Printf(TEXT("%s must be finite and at least %g."), PropertyName, Minimum);
		return false;
	}

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool ValidateAction(const FTDDamageAction& Action, FString& OutError)
	{
		if (Action.Type > ETDDamageActionType::StopHoming)
		{
			OutError = TEXT("Type is not a supported damage action.");
			return false;
		}

		if (!ValidateMinimum(TEXT("DelaySeconds"), Action.DelaySeconds, 0.f, OutError))
		{
			return false;
		}

		if (Action.Type == ETDDamageActionType::ApplyHoming && !Action.Homing.Validate(OutError))
		{
			OutError = FString::Printf(TEXT("Homing: %s"), *OutError);
			return false;
		}

		if (Action.Status && !IsValid(Action.Status))
		{
			OutError = TEXT("Status references an invalid status definition.");
			return false;
		}

		if (Action.Entity && !IsValid(Action.Entity))
		{
			OutError = TEXT("Entity references an invalid damage definition.");
			return false;
		}

		if (Action.Type == ETDDamageActionType::Damage || Action.Type == ETDDamageActionType::ApplyStatus)
		{
			if (!ValidateMinimum(TEXT("Magnitude.Base"), Action.Magnitude.Base, 0.f, OutError)
				|| !ValidateMinimum(TEXT("Magnitude.PerLevel"), Action.Magnitude.PerLevel, 0.f, OutError)
				|| !ValidateMinimum(TEXT("Magnitude.AttackRatio"), Action.Magnitude.AttackRatio, 0.f, OutError)
				|| !ValidateMinimum(TEXT("Magnitude.SpellRatio"), Action.Magnitude.SpellRatio, 0.f, OutError))
			{
				return false;
			}
		}

		if (Action.Type == ETDDamageActionType::Damage && Action.Element > ETDDamageElement::Arcane)
		{
			OutError = TEXT("Element is not a supported damage element.");
			return false;
		}

		if (Action.Type == ETDDamageActionType::ApplyStatus && !IsValid(Action.Status))
		{
			OutError = TEXT("ApplyStatus requires a Status definition.");
			return false;
		}

		if (Action.Type != ETDDamageActionType::SpawnEntity)
		{
			return true;
		}

		if (!IsValid(Action.Entity))
		{
			OutError = TEXT("SpawnEntity requires an Entity definition.");
			return false;
		}

		if (Action.SpawnCount < 1 || Action.SpawnCount > 32)
		{
			OutError = TEXT("SpawnCount must be between 1 and 32.");
			return false;
		}

		if (Action.SpawnAnchor > ETDDamageSpawnAnchor::CastTarget || Action.SpawnDirection > ETDDamageDirection::Down)
		{
			OutError = TEXT("SpawnAnchor and SpawnDirection must use supported enum values.");
			return false;
		}

		if (!IsFiniteVector(Action.SpawnOffset))
		{
			OutError = TEXT("SpawnOffset must be finite on every axis.");
			return false;
		}

		return ValidateMinimum(TEXT("ScatterRadius"), Action.ScatterRadius, 0.f, OutError);
	}
}

bool FTDHomingSettings::Validate(FString& OutError) const
{
	OutError.Reset();
	if (TargetSelection > ETDHomingTargetSelection::EventTarget)
	{
		OutError = TEXT("TargetSelection must be NearestEnemy or EventTarget.");
		return false;
	}

	if (TargetLossPolicy > ETDHomingTargetLossPolicy::ContinueStraight)
	{
		OutError = TEXT("TargetLossPolicy must be Reacquire or ContinueStraight.");
		return false;
	}

	if (!ValidateMinimum(TEXT("SearchRadius"), SearchRadius, 1.f, OutError)
		|| !ValidateMinimum(TEXT("RetargetInterval"), RetargetInterval, 0.02f, OutError))
	{
		return false;
	}

	if (!FMath::IsFinite(TurnRateDegreesPerSecond) || TurnRateDegreesPerSecond <= 0.f)
	{
		OutError = TEXT("TurnRateDegreesPerSecond must be finite and greater than zero.");
		return false;
	}

	return true;
}

bool FTDDamageRule::Validate(FString& OutError) const
{
	OutError.Reset();
	if (Event > ETDDamageEvent::Activate)
	{
		OutError = TEXT("Event is not a supported damage event.");
		return false;
	}

	for (int32 ActionIndex = 0; ActionIndex < Actions.Num(); ++ActionIndex)
	{
		if (!ValidateAction(Actions[ActionIndex], OutError))
		{
			OutError = FString::Printf(TEXT("Actions[%d]: %s"), ActionIndex, *OutError);
			return false;
		}

		if (Actions[ActionIndex].DelaySeconds > 0.f
			&& (Event == ETDDamageEvent::End || Event == ETDDamageEvent::Expire))
		{
			OutError = FString::Printf(TEXT("Actions[%d]: DelaySeconds must be zero for End and Expire events because the damage entity is ending."), ActionIndex);
			return false;
		}
	}

	return true;
}

bool UTDDamageDefinition::ValidateDefinition(FString& OutError) const
{
	OutError.Reset();
	if (Mode > ETDDamageEntityMode::Shockwave || TargetPolicy > ETDDamageTargetPolicy::Everyone)
	{
		OutError = TEXT("Mode and TargetPolicy must use supported enum values.");
		return false;
	}

	if (!ValidateMinimum(TEXT("Cooldown"), Cooldown, 0.f, OutError)
		|| !ValidateMinimum(TEXT("CastRange"), CastRange, 1.f, OutError)
		|| !ValidateMinimum(TEXT("Lifetime"), Lifetime, 0.01f, OutError)
		|| !ValidateMinimum(TEXT("ActivationDelay"), ActivationDelay, 0.f, OutError)
		|| !ValidateMinimum(TEXT("PulseInterval"), PulseInterval, 0.02f, OutError)
		|| !ValidateMinimum(TEXT("Radius"), Radius, 1.f, OutError)
		|| !ValidateMinimum(TEXT("InnerRadius"), InnerRadius, 0.f, OutError)
		|| !ValidateMinimum(TEXT("HalfHeight"), HalfHeight, 1.f, OutError)
		|| !ValidateMinimum(TEXT("ExpansionSpeed"), ExpansionSpeed, 0.f, OutError)
		|| !ValidateMinimum(TEXT("ProjectileSpeed"), ProjectileSpeed, 1.f, OutError)
		|| !ValidateMinimum(TEXT("ProjectileRadius"), ProjectileRadius, 1.f, OutError)
		|| !ValidateMinimum(TEXT("HitInterval"), HitInterval, 0.02f, OutError))
	{
		return false;
	}

	if (Lifetime <= ActivationDelay)
	{
		OutError = TEXT("Lifetime must be greater than ActivationDelay.");
		return false;
	}

	if (Mode == ETDDamageEntityMode::Shockwave && InnerRadius >= Radius)
	{
		OutError = TEXT("Shockwave InnerRadius must be smaller than Radius.");
		return false;
	}

	if (MaxHitsPerTarget < 0)
	{
		OutError = TEXT("MaxHitsPerTarget must be nonnegative; zero allows repeated hits without a count limit.");
		return false;
	}

	if (!IsFiniteVector(VisualScale))
	{
		OutError = TEXT("VisualScale must be finite on every axis.");
		return false;
	}

	if (!FMath::IsFinite(DebugColor.R) || !FMath::IsFinite(DebugColor.G)
		|| !FMath::IsFinite(DebugColor.B) || !FMath::IsFinite(DebugColor.A))
	{
		OutError = TEXT("DebugColor must have finite components.");
		return false;
	}

	for (int32 RuleIndex = 0; RuleIndex < Rules.Num(); ++RuleIndex)
	{
		const FTDDamageRule& Rule = Rules[RuleIndex];
		if (!Rule.Validate(OutError))
		{
			OutError = FString::Printf(TEXT("Rules[%d]: %s"), RuleIndex, *OutError);
			return false;
		}

		for (int32 ActionIndex = 0; ActionIndex < Rule.Actions.Num(); ++ActionIndex)
		{
			const ETDDamageActionType ActionType = Rule.Actions[ActionIndex].Type;
			if (Mode != ETDDamageEntityMode::Projectile
				&& (ActionType == ETDDamageActionType::ApplyHoming || ActionType == ETDDamageActionType::StopHoming))
			{
				OutError = FString::Printf(TEXT("Rules[%d]: Actions[%d]: ApplyHoming and StopHoming require Mode to be Projectile."), RuleIndex, ActionIndex);
				return false;
			}
		}
	}

	return true;
}

#if WITH_EDITOR
EDataValidationResult UTDDamageDefinition::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	FString Error;
	if (!ValidateDefinition(Error))
	{
		Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}

	return ParentResult == EDataValidationResult::Invalid ? ParentResult : EDataValidationResult::Valid;
}
#endif
