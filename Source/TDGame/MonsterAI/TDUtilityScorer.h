#pragma once

#include "CoreMinimal.h"
#include "MonsterAI/TDMonsterDefinition.h"

struct FTDBrainDecision
{
	int32 ActionIndex = INDEX_NONE;
	float Score = 0.f;
	bool bKeptCurrent = false;
};

struct FTDBrainMemory
{
	int32 CurrentActionIndex = INDEX_NONE;
	int32 HoldRemainingSteps = 0;
	TConstArrayView<int32> ActionCooldownSteps;
};

class TDGAME_API FTDUtilityScorer
{
public:
	static float ScoreAction(const FTDResolvedAction& Action, const FTDBrainInputs& Inputs);
	static FTDBrainDecision Think(const FTDResolvedMonsterDefinition& Definition, int32 PhaseIndex, const FTDBrainInputs& Inputs, const FTDBrainMemory& Memory);
};
