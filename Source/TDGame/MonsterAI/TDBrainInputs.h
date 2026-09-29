#pragma once

#include "CoreMinimal.h"

struct FTDNeighborEntry
{
	float Distance = 0.f;
	uint32 SimulationId = 0;
};

struct FTDBrainInputs
{
	static constexpr int32 MaxNeighbors = 8;

	float SelfHealthRatio = 1.f;
	bool bHasTarget = false;
	float DistanceToTarget = 0.f;
	float FacingDot = 1.f;
	bool bHasLineOfSight = true;
	bool bIsTargetAttacking = false;
	uint64 ReadyAbilityMask = 0;
	int32 NeighborCount = 0;
	FTDNeighborEntry Neighbors[MaxNeighbors];

	bool IsAbilityReady(const int32 AbilityIndex) const
	{
		return AbilityIndex >= 0 && AbilityIndex < 64 && (ReadyAbilityMask & (uint64(1) << AbilityIndex)) != 0;
	}

	int32 CountAlliesWithin(const float Radius) const
	{
		int32 Count = 0;
		for (int32 Index = 0; Index < NeighborCount; ++Index)
		{
			if (Neighbors[Index].Distance <= Radius)
			{
				++Count;
			}
		}
		return Count;
	}
};

struct FTDInputArgs
{
	int32 AbilityIndex = INDEX_NONE;
	float Radius = 0.f;
};
