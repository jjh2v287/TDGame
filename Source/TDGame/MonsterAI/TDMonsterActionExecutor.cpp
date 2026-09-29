#include "MonsterAI/TDMonsterActionExecutor.h"
#include "MonsterAI/TDMonsterBody.h"

namespace
{
	FVector2D ToPlanar(const FVector& Vector)
	{
		return FVector2D(Vector.X, Vector.Y);
	}

	FVector2D GetDirectionToTarget(const FTDMonsterStepContext& Context, float& OutDistance)
	{
		const FVector2D Offset = ToPlanar(Context.TargetLocation - Context.SelfLocation);
		OutDistance = Offset.Size();
		return OutDistance > KINDA_SMALL_NUMBER ? Offset / OutDistance : FVector2D::ZeroVector;
	}

	bool IsInSequence(const FTDMonsterBrainFragment& Brain)
	{
		return Brain.SequenceIndex != INDEX_NONE;
	}
}

void FTDMonsterActionExecutor::ResetForDefinition(FTDMonsterBrainFragment& Brain)
{
	const FTDResolvedMonsterDefinition& Definition = *Brain.Definition;
	Brain.PhaseIndex = 0;
	Brain.CurrentActionIndex = INDEX_NONE;
	Brain.CurrentScore = 0.f;
	Brain.HoldSteps = 0;
	Brain.bForceThink = true;
	Brain.State = ETDMonsterFsmState::Idle;
	Brain.ActivePrimitive = ETDMonsterPrimitive::Wait;
	Brain.ActiveArgs = FTDActionArgs();
	Brain.StateStepsRemaining = 0;
	Brain.SequenceIndex = INDEX_NONE;
	Brain.SequenceCursor = 0;
	Brain.ActionCooldownSteps.Init(0, Definition.GetMaxActionCount());
	Brain.AbilityCooldownSteps.Init(0, Definition.Abilities.Num());
}

void FTDMonsterActionExecutor::TickCounters(FTDMonsterBrainFragment& Brain)
{
	Brain.HoldSteps = FMath::Max(0, Brain.HoldSteps - 1);
	Brain.EngageLockSteps = FMath::Max(0, Brain.EngageLockSteps - 1);
	Brain.ReengageBlockSteps = FMath::Max(0, Brain.ReengageBlockSteps - 1);
	for (int32& Cooldown : Brain.ActionCooldownSteps)
	{
		Cooldown = FMath::Max(0, Cooldown - 1);
	}
	for (int32& Cooldown : Brain.AbilityCooldownSteps)
	{
		Cooldown = FMath::Max(0, Cooldown - 1);
	}
}

void FTDMonsterActionExecutor::ApplyDecision(FTDMonsterBrainFragment& Brain, const FTDBrainDecision& Decision, const FTDMonsterStepContext& Context)
{
	if (Decision.ActionIndex == INDEX_NONE)
	{
		Brain.CurrentActionIndex = INDEX_NONE;
		Brain.CurrentScore = 0.f;
		Brain.State = ETDMonsterFsmState::Idle;
		Brain.ActivePrimitive = ETDMonsterPrimitive::Wait;
		Brain.StateStepsRemaining = 0;
		return;
	}
	Brain.CurrentScore = Decision.Score;
	if (Decision.bKeptCurrent)
	{
		return;
	}
	const TArray<FTDResolvedAction>& Actions = Brain.Definition->GetActions(Brain.PhaseIndex);
	const FTDResolvedAction& Action = Actions[Decision.ActionIndex];
	Brain.CurrentActionIndex = Decision.ActionIndex;
	Brain.HoldSteps = Brain.Definition->MinHoldSteps;
	if (Brain.ActionCooldownSteps.IsValidIndex(Decision.ActionIndex))
	{
		Brain.ActionCooldownSteps[Decision.ActionIndex] = Action.CooldownSteps;
	}
	StartPrimitive(Brain, Action.Primitive, Action.Args, Context);
}

void FTDMonsterActionExecutor::EnterPhase(FTDMonsterBrainFragment& Brain, const int32 NewPhaseIndex, const FTDMonsterStepContext& Context)
{
	Brain.PhaseIndex = NewPhaseIndex;
	Brain.CurrentActionIndex = INDEX_NONE;
	Brain.CurrentScore = 0.f;
	Brain.HoldSteps = 0;
	Brain.ActionCooldownSteps.Init(0, Brain.Definition->GetMaxActionCount());
	if (Context.Body)
	{
		Context.Body->ApplyBodyStats(Brain.Definition->GetStatsForPhase(NewPhaseIndex));
	}
	const FTDResolvedPhase& Phase = Brain.Definition->Phases[NewPhaseIndex - 1];
	if (Phase.OnEnterSequenceIndex != INDEX_NONE)
	{
		StartSequence(Brain, Phase.OnEnterSequenceIndex, Context);
	}
}

void FTDMonsterActionExecutor::EnterStagger(FTDMonsterBrainFragment& Brain, const FVector& SourceLocation, const FTDMonsterStepContext& Context)
{
	InterruptActivity(Brain, Context);
	Brain.State = ETDMonsterFsmState::Stagger;
	Brain.StateStepsRemaining = TDMonsterAI::SecondsToSteps(StaggerSeconds);
	if (Context.Body)
	{
		Context.Body->BeginBodyStagger(SourceLocation, StaggerSeconds);
	}
}

void FTDMonsterActionExecutor::InterruptActivity(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context)
{
	if (Brain.State == ETDMonsterFsmState::Cast && Context.Body)
	{
		Context.Body->CancelBodyAbility();
	}
	Brain.SequenceIndex = INDEX_NONE;
	Brain.SequenceCursor = 0;
	Brain.CurrentActionIndex = INDEX_NONE;
	Brain.CurrentScore = 0.f;
	Brain.HoldSteps = 0;
	Brain.State = ETDMonsterFsmState::Idle;
	Brain.ActivePrimitive = ETDMonsterPrimitive::Wait;
	Brain.StateStepsRemaining = 0;
	Brain.bForceThink = true;
}

FTDMonsterMoveIntent FTDMonsterActionExecutor::StepState(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context)
{
	FTDMonsterMoveIntent Intent;
	Intent.bShouldFaceTarget = Brain.bIsEngaged && Context.Target != nullptr;
	switch (Brain.State)
	{
	case ETDMonsterFsmState::Move:
	{
		bool bHasArrived = false;
		Intent = ComputeMoveIntent(Brain, Context, bHasArrived);
		if (!IsInSequence(Brain))
		{
			return Intent;
		}
		Brain.StateStepsRemaining = FMath::Max(0, Brain.StateStepsRemaining - 1);
		if (bHasArrived || Brain.StateStepsRemaining == 0)
		{
			CompleteActivity(Brain, Context);
		}
		return Intent;
	}
	case ETDMonsterFsmState::Cast:
		Intent.bShouldFaceTarget = false;
		Brain.StateStepsRemaining = FMath::Max(0, Brain.StateStepsRemaining - 1);
		if (Brain.StateStepsRemaining == 0)
		{
			CompleteActivity(Brain, Context);
		}
		return Intent;
	case ETDMonsterFsmState::Stagger:
		Intent.bShouldFaceTarget = false;
		Brain.StateStepsRemaining = FMath::Max(0, Brain.StateStepsRemaining - 1);
		if (Brain.StateStepsRemaining == 0)
		{
			Brain.State = ETDMonsterFsmState::Idle;
			Brain.bForceThink = true;
		}
		return Intent;
	case ETDMonsterFsmState::Idle:
	case ETDMonsterFsmState::Sequence:
		if (Brain.StateStepsRemaining > 0)
		{
			Brain.StateStepsRemaining = FMath::Max(0, Brain.StateStepsRemaining - 1);
			if (Brain.StateStepsRemaining == 0)
			{
				CompleteActivity(Brain, Context);
			}
		}
		return Intent;
	}
	return Intent;
}

void FTDMonsterActionExecutor::StartPrimitive(FTDMonsterBrainFragment& Brain, const ETDMonsterPrimitive Primitive, const FTDActionArgs& Args, const FTDMonsterStepContext& Context)
{
	Brain.ActivePrimitive = Primitive;
	Brain.ActiveArgs = Args;
	switch (Primitive)
	{
	case ETDMonsterPrimitive::MoveToward:
	case ETDMonsterPrimitive::MoveAway:
	case ETDMonsterPrimitive::MoveToBand:
	{
		Brain.State = ETDMonsterFsmState::Move;
		const float TimeoutSeconds = Args.Seconds > 0.f ? Args.Seconds : SequenceMoveTimeoutSeconds;
		Brain.StateStepsRemaining = IsInSequence(Brain) ? FMath::Max(1, TDMonsterAI::SecondsToSteps(TimeoutSeconds)) : 0;
		return;
	}
	case ETDMonsterPrimitive::FaceTarget:
		Brain.State = ETDMonsterFsmState::Idle;
		Brain.StateStepsRemaining = FaceTargetSteps;
		if (Context.Body && Context.Target)
		{
			Context.Body->FaceBodyToward(Context.TargetLocation);
		}
		return;
	case ETDMonsterPrimitive::Wait:
	case ETDMonsterPrimitive::PlayEmote:
		Brain.State = ETDMonsterFsmState::Idle;
		Brain.StateStepsRemaining = Args.Seconds > 0.f ? FMath::Max(1, TDMonsterAI::SecondsToSteps(Args.Seconds)) : Brain.Definition->ThinkPeriodSteps;
		return;
	case ETDMonsterPrimitive::CastAbility:
		if (!StartCast(Brain, Args, Context))
		{
			Brain.State = ETDMonsterFsmState::Idle;
			Brain.StateStepsRemaining = 1;
		}
		return;
	case ETDMonsterPrimitive::PlaySequence:
		StartSequence(Brain, Args.SequenceIndex, Context);
		return;
	}
}

bool FTDMonsterActionExecutor::StartCast(FTDMonsterBrainFragment& Brain, const FTDActionArgs& Args, const FTDMonsterStepContext& Context)
{
	const FTDResolvedMonsterDefinition& Definition = *Brain.Definition;
	if (!Context.Body || !Context.Target || !Definition.Abilities.IsValidIndex(Args.AbilityIndex))
	{
		return false;
	}
	if (Brain.AbilityCooldownSteps[Args.AbilityIndex] > 0 || !Context.bHasAttackToken)
	{
		return false;
	}
	const FTDResolvedAbility& Ability = Definition.Abilities[Args.AbilityIndex];
	FTDMonsterAbilityRequest Request;
	Request.AbilityName = Ability.Name;
	Request.Spell = Ability.Spell;
	Request.Range = Ability.Range;
	Request.Target = Context.Target;
	Request.TargetLocation = Context.TargetLocation;
	if (Args.bLeadTarget)
	{
		const float Distance = FVector::Dist2D(Context.SelfLocation, Context.TargetLocation);
		const float LeadSeconds = FMath::Min(0.8f, Distance / 950.f);
		Request.TargetLocation += FVector(Context.TargetVelocity.X, Context.TargetVelocity.Y, 0.f) * LeadSeconds;
	}
	const float DurationSeconds = Context.Body->BeginBodyAbility(Request);
	if (DurationSeconds <= 0.f)
	{
		return false;
	}
	Brain.State = ETDMonsterFsmState::Cast;
	Brain.StateStepsRemaining = FMath::Max(1, TDMonsterAI::SecondsToSteps(DurationSeconds));
	Brain.AbilityCooldownSteps[Args.AbilityIndex] = Ability.CooldownSteps;
	return true;
}

void FTDMonsterActionExecutor::StartSequence(FTDMonsterBrainFragment& Brain, const int32 SequenceIndex, const FTDMonsterStepContext& Context)
{
	if (!Brain.Definition->Sequences.IsValidIndex(SequenceIndex) || Brain.Definition->Sequences[SequenceIndex].IsEmpty())
	{
		Brain.State = ETDMonsterFsmState::Idle;
		Brain.StateStepsRemaining = 1;
		return;
	}
	Brain.SequenceIndex = SequenceIndex;
	Brain.SequenceCursor = 0;
	const FTDResolvedStep& FirstStep = Brain.Definition->Sequences[SequenceIndex][0];
	StartPrimitive(Brain, FirstStep.Primitive, FirstStep.Args, Context);
}

void FTDMonsterActionExecutor::CompleteActivity(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context)
{
	if (IsInSequence(Brain))
	{
		const TArray<FTDResolvedStep>& Steps = Brain.Definition->Sequences[Brain.SequenceIndex];
		++Brain.SequenceCursor;
		if (Steps.IsValidIndex(Brain.SequenceCursor))
		{
			StartPrimitive(Brain, Steps[Brain.SequenceCursor].Primitive, Steps[Brain.SequenceCursor].Args, Context);
			return;
		}
		Brain.SequenceIndex = INDEX_NONE;
		Brain.SequenceCursor = 0;
	}
	Brain.State = ETDMonsterFsmState::Idle;
	Brain.ActivePrimitive = ETDMonsterPrimitive::Wait;
	Brain.StateStepsRemaining = 0;
	Brain.CurrentActionIndex = INDEX_NONE;
	Brain.CurrentScore = 0.f;
	Brain.HoldSteps = 0;
	Brain.bForceThink = true;
}

FTDMonsterMoveIntent FTDMonsterActionExecutor::ComputeMoveIntent(const FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context, bool& bOutHasArrived)
{
	FTDMonsterMoveIntent Intent;
	Intent.bShouldFaceTarget = true;
	bOutHasArrived = false;
	if (!Context.Target)
	{
		bOutHasArrived = true;
		return Intent;
	}
	float Distance = 0.f;
	const FVector2D ToTarget = GetDirectionToTarget(Context, Distance);
	const FTDActionArgs& Args = Brain.ActiveArgs;
	switch (Brain.ActivePrimitive)
	{
	case ETDMonsterPrimitive::MoveToward:
		if (Distance <= Args.StopAtDistance)
		{
			bOutHasArrived = true;
			return Intent;
		}
		Intent.Direction = ToTarget;
		break;
	case ETDMonsterPrimitive::MoveAway:
		Intent.Direction = -ToTarget;
		Intent.bShouldFaceTarget = false;
		break;
	case ETDMonsterPrimitive::MoveToBand:
		if (Distance < Args.BandMin)
		{
			Intent.Direction = -ToTarget;
			break;
		}
		if (Distance > Args.BandMax)
		{
			Intent.Direction = ToTarget;
			break;
		}
		bOutHasArrived = true;
		return Intent;
	default:
		bOutHasArrived = true;
		return Intent;
	}
	Intent.SpeedScale = Args.SpeedScale;
	return Intent;
}
