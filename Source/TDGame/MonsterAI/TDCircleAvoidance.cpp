#include "MonsterAI/TDCircleAvoidance.h"
#include "MonsterAI/TDNeighborGrid.h"

namespace
{
	constexpr double CircleAvoidanceAngleCosines[FTDCircleAvoidance::AngleCount] = {
		1.0,
		0.96592582628906831, 0.96592582628906831,
		0.86602540378443865, 0.86602540378443865,
		0.70710678118654752, 0.70710678118654752,
		0.5, 0.5,
		0.0, 0.0,
		-0.5, -0.5 };

	constexpr double CircleAvoidanceAngleSines[FTDCircleAvoidance::AngleCount] = {
		0.0,
		0.25881904510252074, -0.25881904510252074,
		0.5, -0.5,
		0.70710678118654752, -0.70710678118654752,
		0.86602540378443865, -0.86602540378443865,
		1.0, -1.0,
		0.86602540378443865, -0.86602540378443865 };

	constexpr double CircleAvoidanceSpeedFractions[FTDCircleAvoidance::SpeedFractionCount] = { 1.0, 0.6, 0.3 };
}

FVector2D FTDCircleAvoidance::MakeCandidateVelocity(const FVector2D& PreferredVelocity, const int32 CandidateIndex)
{
	const double PreferredSpeed = PreferredVelocity.Size();
	if (CandidateIndex < 0 || CandidateIndex >= StopCandidateIndex || PreferredSpeed <= 0.0)
	{
		return FVector2D::ZeroVector;
	}
	const FVector2D Heading = PreferredVelocity / PreferredSpeed;
	const int32 AngleIndex = CandidateIndex % AngleCount;
	const double Cosine = CircleAvoidanceAngleCosines[AngleIndex];
	const double Sine = CircleAvoidanceAngleSines[AngleIndex];
	const FVector2D RotatedHeading(Cosine * Heading.X - Sine * Heading.Y, Sine * Heading.X + Cosine * Heading.Y);
	return RotatedHeading * (CircleAvoidanceSpeedFractions[CandidateIndex / AngleCount] * PreferredSpeed);
}

double FTDCircleAvoidance::ComputeTimeToCollision(const FVector2D& CandidateVelocity, const FVector2D& RelativeLocation, const FVector2D& ObstacleVelocity, const double CombinedRadius)
{
	const FVector2D RelativeVelocity = CandidateVelocity - ObstacleVelocity;
	const double Approach = FVector2D::DotProduct(RelativeVelocity, RelativeLocation);
	const bool bIsApproaching = Approach > 0.0;
	const double SurfaceTerm = RelativeLocation.SizeSquared() - CombinedRadius * CombinedRadius;
	if (SurfaceTerm < 0.0)
	{
		return bIsApproaching ? 0.0 : NoCollisionSeconds;
	}
	const double RelativeSpeedSquared = RelativeVelocity.SizeSquared();
	if (!bIsApproaching || RelativeSpeedSquared <= MinimumRelativeSpeedSquared)
	{
		return NoCollisionSeconds;
	}
	const double Discriminant = Approach * Approach - RelativeSpeedSquared * SurfaceTerm;
	if (Discriminant <= 0.0)
	{
		return NoCollisionSeconds;
	}
	return (Approach - FMath::Sqrt(Discriminant)) / RelativeSpeedSquared;
}

double FTDCircleAvoidance::ComputeMinimumTimeToCollision(const FVector2D& SelfLocation, const float SelfRadius, const FVector2D& Velocity, const TConstArrayView<FTDAvoidanceObstacle> Obstacles)
{
	double MinimumSeconds = NoCollisionSeconds;
	for (const FTDAvoidanceObstacle& Obstacle : Obstacles)
	{
		const double CombinedRadius = static_cast<double>(SelfRadius) + Obstacle.Radius + CombinedRadiusMargin;
		MinimumSeconds = FMath::Min(MinimumSeconds, ComputeTimeToCollision(Velocity, Obstacle.Location - SelfLocation, Obstacle.Velocity, CombinedRadius));
	}
	return MinimumSeconds;
}

void FTDCircleAvoidance::ComputeCandidateCosts(const FTDAvoidanceAgent& Agent, const TConstArrayView<FTDAvoidanceObstacle> Obstacles, TStaticArray<double, CandidateCount>& OutCosts)
{
	const double MaxSpeed = Agent.MaxSpeed;
	for (int32 CandidateIndex = 0; CandidateIndex < CandidateCount; ++CandidateIndex)
	{
		const FVector2D Candidate = MakeCandidateVelocity(Agent.PreferredVelocity, CandidateIndex);
		const double CollisionSeconds = ComputeMinimumTimeToCollision(Agent.Location, Agent.Radius, Candidate, Obstacles);
		const double CollisionPenalty = CollisionSeconds >= TimeHorizonSeconds ? 0.0 : TimeToCollisionWeight * (1.0 / FMath::Max(CollisionSeconds, MinimumTimeToCollision) - 1.0 / TimeHorizonSeconds);
		const double PreferenceCost = (Candidate - Agent.PreferredVelocity).Size() / MaxSpeed;
		const double SmoothnessCost = SmoothnessWeight * (Candidate - Agent.PreviousCommandVelocity).Size() / MaxSpeed;
		const double StopCost = CandidateIndex == StopCandidateIndex ? StopPenalty : 0.0;
		OutCosts[CandidateIndex] = PreferenceCost + CollisionPenalty + SmoothnessCost + StopCost;
	}
}

FTDAvoidanceCommand FTDCircleAvoidance::ComputeAvoidanceCommand(const FTDAvoidanceAgent& Agent, const TConstArrayView<FTDAvoidanceObstacle> Obstacles)
{
	FTDAvoidanceCommand Command;
	Command.Velocity = Agent.PreferredVelocity;
	if (Agent.MaxSpeed <= 0.f)
	{
		return Command;
	}
	const bool bNeedsSampling = ComputeMinimumTimeToCollision(Agent.Location, Agent.Radius, Agent.PreferredVelocity, Obstacles) < TimeHorizonSeconds;
	if (bNeedsSampling)
	{
		Command.CandidateIndex = SelectCandidate(Agent, Obstacles);
		Command.Velocity = MakeCandidateVelocity(Agent.PreferredVelocity, Command.CandidateIndex);
	}
	Command.Velocity = ApplyTouchSeparation(Agent, Obstacles, Command.Velocity);
	Command.bIsSlowedWaiting = Command.Velocity.Size() < SlowedWaitSpeedRatio * Agent.PreferredVelocity.Size();
	return Command;
}

FVector2D FTDCircleAvoidance::ComputeYieldVelocity(const FTDAvoidanceAgent& Agent, const TConstArrayView<FTDAvoidanceObstacle> Neighbors)
{
	int32 PickedIndex = INDEX_NONE;
	double PickedSeconds = NoCollisionSeconds;
	for (int32 NeighborIndex = 0; NeighborIndex < Neighbors.Num(); ++NeighborIndex)
	{
		const FTDAvoidanceObstacle& Neighbor = Neighbors[NeighborIndex];
		if (!Neighbor.bIsMonster || Neighbor.Velocity.Size() <= YieldMovingSpeedThreshold)
		{
			continue;
		}
		const double CombinedRadius = static_cast<double>(Agent.Radius) + Neighbor.Radius + CombinedRadiusMargin;
		const double CollisionSeconds = ComputeTimeToCollision(FVector2D::ZeroVector, Neighbor.Location - Agent.Location, Neighbor.Velocity, CombinedRadius);
		if (CollisionSeconds >= YieldTimeToCollisionSeconds)
		{
			continue;
		}
		const bool bIsLaterThanPicked = PickedIndex != INDEX_NONE
			&& (CollisionSeconds > PickedSeconds || (CollisionSeconds == PickedSeconds && Neighbor.SimulationId > Neighbors[PickedIndex].SimulationId));
		if (bIsLaterThanPicked)
		{
			continue;
		}
		PickedIndex = NeighborIndex;
		PickedSeconds = CollisionSeconds;
	}
	if (PickedIndex == INDEX_NONE)
	{
		return FVector2D::ZeroVector;
	}
	const FTDAvoidanceObstacle& Mover = Neighbors[PickedIndex];
	const FVector2D Heading = Mover.Velocity / Mover.Velocity.Size();
	const FVector2D LeftNormal(-Heading.Y, Heading.X);
	const FVector2D AwayFromMover = Agent.Location - Mover.Location;
	double Side = FVector2D::CrossProduct(Mover.Velocity, AwayFromMover);
	if (FMath::Abs(Side) < YieldSideTolerance)
	{
		Side = Agent.SimulationId % 2 == 0 ? 1.0 : -1.0;
	}
	const FVector2D YieldDirection = Side > 0.0 ? LeftNormal : -LeftNormal;
	return YieldDirection * (YieldSpeedRatio * Agent.MaxSpeed);
}

void FTDCircleAvoidance::AppendNearbyExternalObstacles(const FVector2D& SelfLocation, const float SelfRadius, const TConstArrayView<FTDAvoidanceObstacle> ExternalBodies, const int32 ExcludedExternalIndex, FTDAvoidanceObstacleList& OutObstacles)
{
	for (int32 ExternalIndex = 0; ExternalIndex < ExternalBodies.Num(); ++ExternalIndex)
	{
		if (ExternalIndex == ExcludedExternalIndex)
		{
			continue;
		}
		const FTDAvoidanceObstacle& ExternalBody = ExternalBodies[ExternalIndex];
		const double Reach = FTDNeighborGrid::AvoidanceSurfaceGap + SelfRadius + ExternalBody.Radius;
		if (FVector2D::DistSquared(SelfLocation, ExternalBody.Location) > Reach * Reach)
		{
			continue;
		}
		OutObstacles.Add(ExternalBody);
	}
}

bool FTDCircleAvoidance::CanYield(const ETDMonsterFsmState ReportedState, const bool bIsFrozen, const bool bIsHoldingEngagedAttacker)
{
	if (bIsFrozen || bIsHoldingEngagedAttacker)
	{
		return false;
	}
	return ReportedState != ETDMonsterFsmState::Cast && ReportedState != ETDMonsterFsmState::Stagger && ReportedState != ETDMonsterFsmState::Sequence;
}

FVector2D FTDCircleAvoidance::ApplyCommandToIntent(const FVector2D& CommandVelocity, const float MoveSpeed, FTDMonsterMoveIntent& InOutIntent)
{
	const double CommandSpeed = CommandVelocity.Size();
	if (CommandSpeed <= MinimumCommandSpeed || MoveSpeed <= 0.f)
	{
		InOutIntent.SpeedScale = 0.f;
		return FVector2D::ZeroVector;
	}
	InOutIntent.Direction = CommandVelocity / CommandSpeed;
	InOutIntent.SpeedScale = static_cast<float>(CommandSpeed / MoveSpeed);
	return CommandVelocity;
}

int32 FTDCircleAvoidance::SelectCandidate(const FTDAvoidanceAgent& Agent, const TConstArrayView<FTDAvoidanceObstacle> Obstacles)
{
	TStaticArray<double, CandidateCount> Costs;
	ComputeCandidateCosts(Agent, Obstacles, Costs);
	int32 BestIndex = 0;
	for (int32 CandidateIndex = 1; CandidateIndex < CandidateCount; ++CandidateIndex)
	{
		if (Costs[CandidateIndex] < Costs[BestIndex])
		{
			BestIndex = CandidateIndex;
		}
	}
	const int32 PreviousIndex = Agent.PreviousCandidateIndex;
	const bool bShouldKeepPrevious = PreviousIndex >= 0 && PreviousIndex < CandidateCount && Costs[PreviousIndex] <= Costs[BestIndex] + HysteresisCost;
	return bShouldKeepPrevious ? PreviousIndex : BestIndex;
}

FVector2D FTDCircleAvoidance::ApplyTouchSeparation(const FTDAvoidanceAgent& Agent, const TConstArrayView<FTDAvoidanceObstacle> Obstacles, const FVector2D& Velocity)
{
	const double Speed = Velocity.Size();
	if (Speed <= MinimumCommandSpeed)
	{
		return Velocity;
	}
	FVector2D Push = FVector2D::ZeroVector;
	for (const FTDAvoidanceObstacle& Obstacle : Obstacles)
	{
		if (!Obstacle.bIsMonster)
		{
			continue;
		}
		const FVector2D Offset = Agent.Location - Obstacle.Location;
		const double Distance = Offset.Size();
		const double SurfaceGap = Distance - Agent.Radius - Obstacle.Radius;
		if (SurfaceGap >= TouchGap || Distance <= MinimumSeparationDistance)
		{
			continue;
		}
		const double DesiredGap = static_cast<double>(Agent.Radius) + Obstacle.Radius + SeparationExtraGap;
		Push += (Offset / Distance) * ((DesiredGap - Distance) / DesiredGap);
	}
	if (Push.IsZero())
	{
		return Velocity;
	}
	const FVector2D Steered = Velocity / Speed + Push * SeparationWeight;
	const double SteeredLength = Steered.Size();
	if (SteeredLength <= MinimumSteeringLength)
	{
		return Velocity;
	}
	return Steered / SteeredLength * Speed;
}
