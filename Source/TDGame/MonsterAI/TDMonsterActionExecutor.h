#pragma once

#include "CoreMinimal.h"
#include "MonsterAI/TDCombatTactics.h"
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
	bool bIsTacticsActive = false;
	bool bHoldsAttackToken = false;
	ETDTacticsSlotKind TacticsSlotKind = ETDTacticsSlotKind::None;
	FVector2D TacticsSlotDirection = FVector2D::ZeroVector;
	float TacticsSlotRadius = 0.f;
	float TacticsSlotHalfAngleDegrees = 0.f;
	bool bIsTargetClosingIn = false;
	bool bIsFeinting = false;
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
	static bool MeasureTacticsSlotGap(const FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context, float& OutGap);

private:
	static void StartPrimitive(FTDMonsterBrainFragment& Brain, ETDMonsterPrimitive Primitive, const FTDActionArgs& Args, const FTDMonsterStepContext& Context);
	static bool StartCast(FTDMonsterBrainFragment& Brain, const FTDActionArgs& Args, const FTDMonsterStepContext& Context);
	static void StartSequence(FTDMonsterBrainFragment& Brain, int32 SequenceIndex, const FTDMonsterStepContext& Context);
	static void CompleteActivity(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context);
	static FTDMonsterMoveIntent ComputeMoveIntent(const FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context, bool& bOutHasArrived);
	static FTDMonsterMoveIntent ComputeWaitSlotIntent(const FTDMonsterStepContext& Context, const FVector2D& ToTarget, float Distance, bool& bOutHasArrived);
	static FTDMonsterMoveIntent ComputeQueueIntent(const FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context, const FVector2D& ToTarget, float Distance, bool& bOutHasArrived);
	static bool ComputeTacticsSlotPoint(const FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context, FVector2D& OutPoint);
};
