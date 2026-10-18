#pragma once

#include "CoreMinimal.h"
#include "MonsterAI/TDMonsterDefinition.h"

struct FTDMonsterBodyFragment
{
	TWeakObjectPtr<AActor> Actor;
	uint32 SimulationId = 0;
	uint32 Generation = 0;
	bool bIsActive = false;
	FVector Location = FVector::ZeroVector;
	FVector SpawnLocation = FVector::ZeroVector;
	FVector HomeLocation = FVector::ZeroVector;
	float Yaw = 0.f;
	float Radius = 40.f;
};

enum class ETDMonsterEngagement : uint8
{
	Home,
	Engaged,
	Returning
};

enum class ETDMonsterAlarmRole : uint8
{
	None,
	Caller,
	Silent,
	Follower
};

struct FTDMonsterBrainFragment
{
	TSharedPtr<const FTDResolvedMonsterDefinition> Definition;
	TWeakObjectPtr<AActor> Target;
	int32 PhaseIndex = 0;
	int32 CurrentActionIndex = INDEX_NONE;
	float CurrentScore = 0.f;
	int32 HoldSteps = 0;
	uint64 NextThinkStep = 0;
	bool bForceThink = true;
	ETDMonsterFsmState State = ETDMonsterFsmState::Idle;
	ETDMonsterPrimitive ActivePrimitive = ETDMonsterPrimitive::Wait;
	FTDActionArgs ActiveArgs;
	int32 StateStepsRemaining = 0;
	int32 SequenceIndex = INDEX_NONE;
	int32 SequenceCursor = 0;
	uint32 CastStartCount = 0;
	int32 LastCastAbilityIndex = INDEX_NONE;
	TArray<int32> ActionCooldownSteps;
	TArray<int32> AbilityCooldownSteps;
	ETDMonsterEngagement Engagement = ETDMonsterEngagement::Home;
	bool bIsFrozen = false;
	int32 EngageLockSteps = 0;
	int32 ReengageBlockSteps = 0;
	int32 OutOfReachStepCount = 0;
	int32 ReturnStallStepCount = 0;
	float ClosestReturnDistance = 0.f;
	ETDMonsterAlarmRole AlarmRole = ETDMonsterAlarmRole::None;
	uint8 AlarmHop = 0;
	uint64 AlarmCallStep = 0;
	float RetreatAnchorDistance = -1.f;
	int32 RetreatStallSteps = 0;
	bool bWasRetreatCornered = false;
	bool bIsFlying = false;
	uint64 FlightChangeStep = 0;

	bool IsEngaged() const
	{
		return Engagement == ETDMonsterEngagement::Engaged;
	}

	ETDMonsterFsmState GetReportedState() const
	{
		if (SequenceIndex != INDEX_NONE && State != ETDMonsterFsmState::Stagger)
		{
			return ETDMonsterFsmState::Sequence;
		}
		return State;
	}

	bool IsInTimedRetreat() const
	{
		return SequenceIndex == INDEX_NONE && State == ETDMonsterFsmState::Move && ActivePrimitive == ETDMonsterPrimitive::MoveAway && StateStepsRemaining > 0;
	}

	bool CanThink() const
	{
		return SequenceIndex == INDEX_NONE && (State == ETDMonsterFsmState::Idle || State == ETDMonsterFsmState::Move) && !IsInTimedRetreat();
	}

	FName GetCurrentActionId() const
	{
		if (!Definition.IsValid())
		{
			return NAME_None;
		}
		const TArray<FTDResolvedAction>& Actions = Definition->GetActions(PhaseIndex);
		return Actions.IsValidIndex(CurrentActionIndex) ? Actions[CurrentActionIndex].Id : NAME_None;
	}
};

struct FTDMonsterMoveIntent
{
	FVector2D Direction = FVector2D::ZeroVector;
	float SpeedScale = 0.f;
	bool bShouldFaceTarget = false;
};

struct FTDMonsterAvoidanceFragment
{
	FVector2D PreviousCommandVelocity = FVector2D::ZeroVector;
	int32 PreviousCandidateIndex = INDEX_NONE;
	int32 SlowedWaitStepCount = 0;
};
