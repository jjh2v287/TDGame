#pragma once

#include "CoreMinimal.h"
#include "MonsterAI/TDMonsterBrainSlot.h"
#include "MonsterAI/TDUtilityScorer.h"

class ITDMonsterBody;

struct FTDMonsterStepContext
{
	ITDMonsterBody* Body = nullptr;
	AActor* Target = nullptr;
	FVector SelfLocation = FVector::ZeroVector;
	FVector TargetLocation = FVector::ZeroVector;
	FVector TargetVelocity = FVector::ZeroVector;
	bool bHasAttackToken = true;
};

class TDGAME_API FTDMonsterActionExecutor
{
public:
	static constexpr float StaggerSeconds = 0.35f;
	static constexpr float SequenceMoveTimeoutSeconds = 2.f;
	static constexpr int32 FaceTargetSteps = 8;

	static void ResetForDefinition(FTDMonsterBrainFragment& Brain);
	static void ApplyDecision(FTDMonsterBrainFragment& Brain, const FTDBrainDecision& Decision, const FTDMonsterStepContext& Context);
	static void EnterPhase(FTDMonsterBrainFragment& Brain, int32 NewPhaseIndex, const FTDMonsterStepContext& Context);
	static void EnterStagger(FTDMonsterBrainFragment& Brain, const FVector& SourceLocation, const FTDMonsterStepContext& Context);
	static void InterruptActivity(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context);
	static void TickCounters(FTDMonsterBrainFragment& Brain);
	static FTDMonsterMoveIntent StepState(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context);

private:
	static void StartPrimitive(FTDMonsterBrainFragment& Brain, ETDMonsterPrimitive Primitive, const FTDActionArgs& Args, const FTDMonsterStepContext& Context);
	static bool StartCast(FTDMonsterBrainFragment& Brain, const FTDActionArgs& Args, const FTDMonsterStepContext& Context);
	static void StartSequence(FTDMonsterBrainFragment& Brain, int32 SequenceIndex, const FTDMonsterStepContext& Context);
	static void CompleteActivity(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context);
	static FTDMonsterMoveIntent ComputeMoveIntent(const FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context, bool& bOutHasArrived);
};
