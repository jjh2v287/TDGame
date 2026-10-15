#pragma once

#include "CoreMinimal.h"
#include "HierarchicalHashGrid2D.h"
#include "MonsterAI/TDMonsterBrainSlot.h"
#include "Templates/Function.h"

struct FTDNeighborCandidate
{
	double SortDistance = 0.0;
	uint32 SimulationId = 0;
	int32 SlotIndex = INDEX_NONE;
};

using FTDNeighborSlotList = TArray<int32, TInlineAllocator<64>>;
using FTDNeighborCandidateList = TArray<FTDNeighborCandidate, TInlineAllocator<32>>;

class TDGAME_API FTDNeighborGrid
{
public:
	static constexpr float CellSize = 250.f;
	static constexpr double SmallQueryCellSpan = 4.0;
	static constexpr double AvoidanceSurfaceGap = 200.0;
	static constexpr int32 MaxAvoidanceNeighbors = 8;

	FTDNeighborGrid();

	void Rebuild(TConstArrayView<FTDMonsterBodyFragment> Bodies, TConstArrayView<int32> OrderedSlots);
	void QuerySlots(const FVector& Center, double HalfExtent, FTDNeighborSlotList& OutSlots) const;
	void GatherAvoidanceNeighbors(TConstArrayView<FTDMonsterBodyFragment> Bodies, int32 SelfSlot, FTDNeighborCandidateList& OutNeighbors) const;
	void GatherNeighborsWithinDistance(TConstArrayView<FTDMonsterBodyFragment> Bodies, int32 SelfSlot, float MaxDistance, int32 MaxCount, TFunctionRef<bool(int32)> IsSlotAccepted, FTDNeighborCandidateList& OutNeighbors) const;

	static void KeepNearestNeighbors(FTDNeighborCandidateList& Candidates, int32 MaxCount);

private:
	THierarchicalHashGrid2D<2, 4, int32> Grid;
	float LargestRadius = 0.f;
};
