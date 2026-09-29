#include "MonsterAI/TDUtilityScorer.h"

float FTDUtilityScorer::ScoreAction(const FTDResolvedAction& Action, const FTDBrainInputs& Inputs)
{
	float Score = Action.Weight;
	for (const FTDResolvedConsideration& Consideration : Action.Considerations)
	{
		if (Score <= 0.f)
		{
			return 0.f;
		}
		Score *= Consideration.Evaluate(Inputs);
	}
	return FMath::Max(Score, 0.f);
}

FTDBrainDecision FTDUtilityScorer::Think(const FTDResolvedMonsterDefinition& Definition, const int32 PhaseIndex, const FTDBrainInputs& Inputs, const FTDBrainMemory& Memory)
{
	const TArray<FTDResolvedAction>& Actions = Definition.GetActions(PhaseIndex);
	FTDBrainDecision Best;
	float CurrentScore = 0.f;
	for (int32 ActionIndex = 0; ActionIndex < Actions.Num(); ++ActionIndex)
	{
		const bool bIsCoolingDown = Memory.ActionCooldownSteps.IsValidIndex(ActionIndex) && Memory.ActionCooldownSteps[ActionIndex] > 0;
		const bool bIsCurrent = ActionIndex == Memory.CurrentActionIndex;
		if (bIsCoolingDown && !bIsCurrent)
		{
			continue;
		}
		const float Score = ScoreAction(Actions[ActionIndex], Inputs);
		if (bIsCurrent)
		{
			CurrentScore = Score;
		}
		if (Score > Best.Score)
		{
			Best.ActionIndex = ActionIndex;
			Best.Score = Score;
		}
	}

	const bool bHasCurrent = Actions.IsValidIndex(Memory.CurrentActionIndex) && CurrentScore > 0.f;
	if (!bHasCurrent)
	{
		return Best;
	}
	const bool bIsHolding = Memory.HoldRemainingSteps > 0;
	const bool bChallengerIsWeak = Best.Score <= CurrentScore * Definition.SwitchRatio;
	if (bIsHolding || Best.ActionIndex == Memory.CurrentActionIndex || bChallengerIsWeak)
	{
		FTDBrainDecision Kept;
		Kept.ActionIndex = Memory.CurrentActionIndex;
		Kept.Score = CurrentScore;
		Kept.bKeptCurrent = true;
		return Kept;
	}
	return Best;
}
