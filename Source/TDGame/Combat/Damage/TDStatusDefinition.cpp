#include "Combat/Damage/TDStatusDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool UTDStatusDefinition::ValidateDefinition(FString& OutError) const
{
	OutError.Reset();
	const auto ValidateMinimum = [&OutError](const TCHAR* PropertyName, float Value, float Minimum)
	{
		if (FMath::IsFinite(Value) && Value >= Minimum)
		{
			return true;
		}

		OutError = FString::Printf(TEXT("%s must be finite and at least %g."), PropertyName, Minimum);
		return false;
	};

	if (!ValidateMinimum(TEXT("Duration"), Duration, 0.02f)
		|| !ValidateMinimum(TEXT("PulseInterval"), PulseInterval, 0.02f)
		|| !ValidateMinimum(TEXT("DamageThreshold"), DamageThreshold, 0.f)
		|| !ValidateMinimum(TEXT("BuildupResetDelay"), BuildupResetDelay, 0.02f))
	{
		return false;
	}

	for (int32 RuleIndex = 0; RuleIndex < Rules.Num(); ++RuleIndex)
	{
		const FTDDamageRule& Rule = Rules[RuleIndex];
		if (Rule.Event != ETDDamageEvent::Spawn && Rule.Event != ETDDamageEvent::Pulse
			&& Rule.Event != ETDDamageEvent::Expire && Rule.Event != ETDDamageEvent::Kill)
		{
			OutError = FString::Printf(TEXT("Rules[%d]: Status definitions support only Spawn, Pulse, Expire, and Kill events."), RuleIndex);
			return false;
		}

		if (!Rule.Validate(OutError))
		{
			OutError = FString::Printf(TEXT("Rules[%d]: %s"), RuleIndex, *OutError);
			return false;
		}

		for (int32 ActionIndex = 0; ActionIndex < Rule.Actions.Num(); ++ActionIndex)
		{
			const FTDDamageAction& Action = Rule.Actions[ActionIndex];
			if (Action.DelaySeconds > 0.f)
			{
				OutError = FString::Printf(TEXT("Rules[%d]: Actions[%d]: Status actions require DelaySeconds to be zero; delayed actions require a damage entity."), RuleIndex, ActionIndex);
				return false;
			}

			if (Action.Type == ETDDamageActionType::ApplyHoming || Action.Type == ETDDamageActionType::StopHoming)
			{
				OutError = FString::Printf(TEXT("Rules[%d]: Actions[%d]: Status definitions cannot use ApplyHoming or StopHoming; place homing actions on a Projectile damage entity."), RuleIndex, ActionIndex);
				return false;
			}
		}
	}

	return true;
}

#if WITH_EDITOR
EDataValidationResult UTDStatusDefinition::IsDataValid(FDataValidationContext& Context) const
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
