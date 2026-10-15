#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "MonsterAI/TDMonsterBrainSlot.h"

struct FTDAvoidanceObstacle
{
	FVector2D Location = FVector2D::ZeroVector;
	FVector2D Velocity = FVector2D::ZeroVector;
	float Radius = 0.f;
	uint32 SimulationId = 0;
	bool bIsMonster = false;
};

struct FTDAvoidanceAgent
{
	FVector2D Location = FVector2D::ZeroVector;
	float Radius = 0.f;
	FVector2D PreferredVelocity = FVector2D::ZeroVector;
	FVector2D PreviousCommandVelocity = FVector2D::ZeroVector;
	int32 PreviousCandidateIndex = INDEX_NONE;
	float MaxSpeed = 0.f;
	uint32 SimulationId = 0;
};

struct FTDAvoidanceCommand
{
	FVector2D Velocity = FVector2D::ZeroVector;
	int32 CandidateIndex = 0;
	bool bIsSlowedWaiting = false;
};

using FTDAvoidanceObstacleList = TArray<FTDAvoidanceObstacle, TInlineAllocator<16>>;

struct TDGAME_API FTDCircleAvoidance
{
	static constexpr int32 AngleCount = 13;
	static constexpr int32 SpeedFractionCount = 3;
	static constexpr int32 CandidateCount = AngleCount * SpeedFractionCount + 1;
	static constexpr int32 StopCandidateIndex = CandidateCount - 1;
	static constexpr double CombinedRadiusMargin = 10.0;
	static constexpr double TouchGap = 0.5;
	static constexpr double SeparationExtraGap = 20.0;
	static constexpr double SeparationWeight = 1.5;
	static constexpr double MinimumSeparationDistance = 1e-4;
	static constexpr double MinimumSteeringLength = 1e-8;
	static constexpr double TimeHorizonSeconds = 1.0;
	static constexpr double MinimumTimeToCollision = 1e-3;
	static constexpr double NoCollisionSeconds = TNumericLimits<double>::Max();
	static constexpr double MinimumRelativeSpeedSquared = 1e-9;
	static constexpr double TimeToCollisionWeight = 0.3;
	static constexpr double SmoothnessWeight = 0.3;
	static constexpr double StopPenalty = 0.2;
	static constexpr double HysteresisCost = 0.1;
	static constexpr double SlowedWaitSpeedRatio = 0.5;
	static constexpr double MinimumPreferredSpeed = 1e-6;
	static constexpr double MinimumCommandSpeed = 1e-3;
	static constexpr double YieldTimeToCollisionSeconds = 1.0;
	static constexpr double YieldSpeedRatio = 0.5;
	static constexpr double YieldMovingSpeedThreshold = 1.0;
	static constexpr double YieldSideTolerance = 1e-6;

	static FVector2D MakeCandidateVelocity(const FVector2D& PreferredVelocity, int32 CandidateIndex);
	static double ComputeTimeToCollision(const FVector2D& CandidateVelocity, const FVector2D& RelativeLocation, const FVector2D& ObstacleVelocity, double CombinedRadius);
	static double ComputeMinimumTimeToCollision(const FVector2D& SelfLocation, float SelfRadius, const FVector2D& Velocity, TConstArrayView<FTDAvoidanceObstacle> Obstacles);
	static void ComputeCandidateCosts(const FTDAvoidanceAgent& Agent, TConstArrayView<FTDAvoidanceObstacle> Obstacles, TStaticArray<double, CandidateCount>& OutCosts);
	static FTDAvoidanceCommand ComputeAvoidanceCommand(const FTDAvoidanceAgent& Agent, TConstArrayView<FTDAvoidanceObstacle> Obstacles);
	static FVector2D ComputeYieldVelocity(const FTDAvoidanceAgent& Agent, TConstArrayView<FTDAvoidanceObstacle> Neighbors);
	static void AppendNearbyExternalObstacles(const FVector2D& SelfLocation, float SelfRadius, TConstArrayView<FTDAvoidanceObstacle> ExternalBodies, int32 ExcludedExternalIndex, FTDAvoidanceObstacleList& OutObstacles);
	static bool CanYield(ETDMonsterFsmState ReportedState, bool bIsFrozen, bool bIsHoldingEngagedAttacker);
	static FVector2D ApplyCommandToIntent(const FVector2D& CommandVelocity, float MoveSpeed, FTDMonsterMoveIntent& InOutIntent);

private:
	static int32 SelectCandidate(const FTDAvoidanceAgent& Agent, TConstArrayView<FTDAvoidanceObstacle> Obstacles);
	static FVector2D ApplyTouchSeparation(const FTDAvoidanceAgent& Agent, TConstArrayView<FTDAvoidanceObstacle> Obstacles, const FVector2D& Velocity);
};
