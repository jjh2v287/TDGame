#include "MonsterAI/TDNeighborGrid.h"

FTDNeighborGrid::FTDNeighborGrid()
	: Grid(CellSize)
{
}

void FTDNeighborGrid::Rebuild(const TConstArrayView<FTDMonsterBodyFragment> Bodies, const TConstArrayView<int32> OrderedSlots)
{
	Grid.Reset();
	LargestRadius = 0.f;
	for (const int32 SlotIndex : OrderedSlots)
	{
		const FTDMonsterBodyFragment& Body = Bodies[SlotIndex];
		if (!Body.bIsActive)
		{
			continue;
		}
		const FVector Extent(Body.Radius, Body.Radius, 0.0);
		Grid.Add(SlotIndex, FBox(Body.Location - Extent, Body.Location + Extent));
		LargestRadius = FMath::Max(LargestRadius, Body.Radius);
	}
}

void FTDNeighborGrid::QuerySlots(const FVector& Center, const double HalfExtent, FTDNeighborSlotList& OutSlots) const
{
	OutSlots.Reset();
	const FVector Extent(HalfExtent, HalfExtent, 0.0);
	const FBox Bounds(Center - Extent, Center + Extent);
	if (2.0 * HalfExtent <= CellSize * SmallQueryCellSpan)
	{
		Grid.QuerySmall(Bounds, OutSlots);
		return;
	}
	Grid.Query(Bounds, OutSlots);
}

void FTDNeighborGrid::GatherAvoidanceNeighbors(const TConstArrayView<FTDMonsterBodyFragment> Bodies, const int32 SelfSlot, FTDNeighborCandidateList& OutNeighbors) const
{
	OutNeighbors.Reset();
	const FTDMonsterBodyFragment& Self = Bodies[SelfSlot];
	FTDNeighborSlotList NearbySlots;
	QuerySlots(Self.Location, AvoidanceSurfaceGap + Self.Radius + LargestRadius, NearbySlots);
	for (const int32 OtherSlot : NearbySlots)
	{
		if (OtherSlot == SelfSlot || !Bodies[OtherSlot].bIsActive)
		{
			continue;
		}
		const FTDMonsterBodyFragment& Other = Bodies[OtherSlot];
		const double Reach = AvoidanceSurfaceGap + Self.Radius + Other.Radius;
		const double DistanceSquared = FVector::DistSquared2D(Self.Location, Other.Location);
		if (DistanceSquared > Reach * Reach)
		{
			continue;
		}
		OutNeighbors.Add({ DistanceSquared, Other.SimulationId, OtherSlot });
	}
	KeepNearestNeighbors(OutNeighbors, MaxAvoidanceNeighbors);
}

void FTDNeighborGrid::GatherNeighborsWithinDistance(const TConstArrayView<FTDMonsterBodyFragment> Bodies, const int32 SelfSlot, const float MaxDistance, const int32 MaxCount, TFunctionRef<bool(int32)> IsSlotAccepted, FTDNeighborCandidateList& OutNeighbors) const
{
	OutNeighbors.Reset();
	const FTDMonsterBodyFragment& Self = Bodies[SelfSlot];
	FTDNeighborSlotList NearbySlots;
	QuerySlots(Self.Location, MaxDistance, NearbySlots);
	for (const int32 OtherSlot : NearbySlots)
	{
		if (OtherSlot == SelfSlot || !Bodies[OtherSlot].bIsActive || !IsSlotAccepted(OtherSlot))
		{
			continue;
		}
		const float Distance = FVector::Dist2D(Self.Location, Bodies[OtherSlot].Location);
		if (Distance > MaxDistance)
		{
			continue;
		}
		OutNeighbors.Add({ Distance, Bodies[OtherSlot].SimulationId, OtherSlot });
	}
	KeepNearestNeighbors(OutNeighbors, MaxCount);
}

void FTDNeighborGrid::KeepNearestNeighbors(FTDNeighborCandidateList& Candidates, const int32 MaxCount)
{
	Candidates.Sort([](const FTDNeighborCandidate& Left, const FTDNeighborCandidate& Right)
	{
		return Left.SortDistance < Right.SortDistance || (Left.SortDistance == Right.SortDistance && Left.SimulationId < Right.SimulationId);
	});
	if (Candidates.Num() > MaxCount)
	{
		Candidates.SetNum(MaxCount, EAllowShrinking::No);
	}
}
