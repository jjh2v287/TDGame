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
	float TacticsBandFloor = 0.f;
	uint64 StepIndex = 0;
	bool bIsDangerLocked = false;
};

class TDGAME_API FTDMonsterActionExecutor
{
public:
	static constexpr float StaggerSeconds = 0.35f;
	static constexpr float SequenceMoveTimeoutSeconds = 2.f;
	static constexpr int32 FaceTargetSteps = 8;
	static constexpr int32 RetreatCorneredSteps = 24;
	static constexpr float RetreatProgressCm = 30.f;
	static constexpr float MinimumChargeDistanceCm = 100.f;
	static constexpr float ChargeOvershootCm = 600.f;
	static constexpr float DefaultChargeSpeedCm = 1400.f;
	static constexpr float ChargeOverrunSeconds = 0.5f;
	static constexpr int32 ChargeGraceSteps = 8;
	static constexpr int32 ChargeStallLimitSteps = 6;
	static constexpr float ChargeStallProgressRatio = 0.25f;
	static constexpr float DefaultChannelSeconds = 1.f;
	static constexpr float ShootdownLandingSeconds = 0.6f;

	static void ResetForDefinition(FTDMonsterBrainFragment& Brain);
	static void ApplyDecision(FTDMonsterBrainFragment& Brain, const FTDBrainDecision& Decision, const FTDMonsterStepContext& Context);
	static void EnterPhase(FTDMonsterBrainFragment& Brain, int32 NewPhaseIndex, const FTDMonsterStepContext& Context);
	static void EnterStagger(FTDMonsterBrainFragment& Brain, const FVector& SourceLocation, const FTDMonsterStepContext& Context);
	static void EnterStagger(FTDMonsterBrainFragment& Brain, const FVector& SourceLocation, const FTDMonsterStepContext& Context, float Seconds);
	static bool ApplyBreakDamage(FTDMonsterBrainFragment& Brain, float Damage, float MaxHealth, const FVector& SourceLocation, const FTDMonsterStepContext& Context);
	static void InterruptActivity(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context);
	static void BeginRetreat(FTDMonsterBrainFragment& Brain, int32 Steps, float SpeedScale, float HoldDistanceCm, const FTDMonsterStepContext& Context);
	static void TickCounters(FTDMonsterBrainFragment& Brain);
	static FTDMonsterMoveIntent StepState(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context);
	static bool MeasureTacticsSlotGap(const FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context, float& OutGap);

private:
	static void StartPrimitive(FTDMonsterBrainFragment& Brain, ETDMonsterPrimitive Primitive, const FTDActionArgs& Args, const FTDMonsterStepContext& Context);
	static bool StartCast(FTDMonsterBrainFragment& Brain, const FTDActionArgs& Args, const FTDMonsterStepContext& Context);
	static void StartTakeOff(FTDMonsterBrainFragment& Brain, const FTDActionArgs& Args, const FTDMonsterStepContext& Context);
	static void StartLand(FTDMonsterBrainFragment& Brain, const FTDActionArgs& Args, const FTDMonsterStepContext& Context);
	static void StartSummonMinions(FTDMonsterBrainFragment& Brain, const FTDActionArgs& Args, const FTDMonsterStepContext& Context);
	static void ArmShootdown(FTDMonsterBrainFragment& Brain, const FTDActionArgs& Args, const FTDMonsterStepContext& Context);
	static bool StartCharge(FTDMonsterBrainFragment& Brain, const FTDActionArgs& Args, const FTDMonsterStepContext& Context);
	static void BeginChargeDash(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context);
	static FTDMonsterMoveIntent StepChargeDash(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context);
	static void FinishCharge(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context);
	static void StartChannel(FTDMonsterBrainFragment& Brain, const FTDActionArgs& Args, const FTDMonsterStepContext& Context);
	static void FinishChannel(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context);
	static void StartSequence(FTDMonsterBrainFragment& Brain, int32 SequenceIndex, const FTDMonsterStepContext& Context);
	static void CompleteActivity(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context);
	static void ResetRetreatTracking(FTDMonsterBrainFragment& Brain);
	static FTDMonsterMoveIntent StepTimedRetreat(FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context);
	static FTDMonsterMoveIntent ComputeMoveIntent(const FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context, bool& bOutHasArrived);
	static FTDMonsterMoveIntent ComputeWaitSlotIntent(const FTDMonsterStepContext& Context, const FVector2D& ToTarget, float Distance, bool& bOutHasArrived);
	static FTDMonsterMoveIntent ComputeQueueIntent(const FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context, const FVector2D& ToTarget, float Distance, bool& bOutHasArrived);
	static bool ComputeTacticsSlotPoint(const FTDMonsterBrainFragment& Brain, const FTDMonsterStepContext& Context, FVector2D& OutPoint);
};
