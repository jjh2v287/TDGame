#include "MonsterAI/TDPackMorale.h"
#include "MonsterAI/TDMonsterDefinition.h"

FTDMonsterMoraleFragment FTDPackMorale::MakeFragment(const FTDResolvedMonsterDefinition& Definition)
{
	FTDMonsterMoraleFragment Fragment;
	Fragment.bIsLeader = Definition.bIsBoss || Definition.bIsElite;
	Fragment.bIsImmune = Fragment.bIsLeader || Definition.CombatRole == ETDMonsterCombatRole::Support;
	return Fragment;
}

void FTDPackMorale::RecordLoss(FTDMonsterMoraleFragment& Fragment, const uint64 StepIndex)
{
	int32 OldestIndex = 0;
	for (int32 Index = 1; Index < FTDMonsterMoraleFragment::LossSlotCount; ++Index)
	{
		if (Fragment.LossSteps[Index] < Fragment.LossSteps[OldestIndex])
		{
			OldestIndex = Index;
		}
	}
	Fragment.LossSteps[OldestIndex] = StepIndex;
}

int32 FTDPackMorale::CountRecentLosses(const FTDMonsterMoraleFragment& Fragment, const uint64 StepIndex)
{
	int32 RecentLossCount = 0;
	for (const uint64 LossStep : Fragment.LossSteps)
	{
		if (LossStep != 0 && StepIndex >= LossStep && StepIndex - LossStep <= static_cast<uint64>(LossWindowSteps))
		{
			++RecentLossCount;
		}
	}
	return RecentLossCount;
}

void FTDPackMorale::ClearLosses(FTDMonsterMoraleFragment& Fragment)
{
	for (uint64& LossStep : Fragment.LossSteps)
	{
		LossStep = 0;
	}
}

bool FTDPackMorale::ShouldPanic(const int32 RecentLossCount, const int32 PanickingAllyCount, const int32 AllySupport)
{
	constexpr int32 LossWeight = 2;
	if (RecentLossCount < MinLossesForPanic || AllySupport < MinSupportForPanic)
	{
		return false;
	}
	return LossWeight * RecentLossCount + PanickingAllyCount >= AllySupport;
}

int32 FTDPackMorale::ComputeFearSteps(const uint32 SimulationId, const uint64 StepIndex, const bool bIsLeaderShock)
{
	const int32 MinSteps = bIsLeaderShock ? LeaderFearMinSteps : FearMinSteps;
	const int32 MaxSteps = bIsLeaderShock ? LeaderFearMaxSteps : FearMaxSteps;
	const uint64 SpanSteps = static_cast<uint64>(MaxSteps - MinSteps + 1);
	return MinSteps + static_cast<int32>((static_cast<uint64>(SimulationId) * 37 + StepIndex) % SpanSteps);
}

int32 FTDPackMorale::GetAllyWeight(const bool bIsImmune)
{
	return bIsImmune ? ImmuneAllyWeight : 1;
}

bool FTDPackMorale::IsInFear(const FTDMonsterMoraleFragment& Fragment)
{
	return Fragment.FearState != ETDPackFearState::None;
}
