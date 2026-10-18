#pragma once

#include "CoreMinimal.h"

struct FTDResolvedMonsterDefinition;

enum class ETDPackFearState : uint8
{
	None,
	Pending,
	Retreating
};

struct FTDMonsterMoraleFragment
{
	static constexpr int32 LossSlotCount = 4;

	bool bIsImmune = false;
	bool bIsLeader = false;
	bool bHasRallied = false;
	bool bLeaderShock = false;
	bool bIsLeaderFear = false;
	ETDPackFearState FearState = ETDPackFearState::None;
	uint64 LossSteps[LossSlotCount] = {};
	uint64 FearStartDeadlineStep = 0;
	uint64 FearEndStep = 0;
	int32 FearSteps = 0;
};

struct FTDPackDeathEvent
{
	FVector Location = FVector::ZeroVector;
	uint32 VictimSimulationId = 0;
	int32 TeamId = 0;
	bool bWasLeader = false;
};

class TDGAME_API FTDPackMorale
{
public:
	static constexpr int32 WitnessRadiusCm = 1200;
	static constexpr int32 LeaderWitnessRadiusCm = 1600;
	static constexpr int32 LossWindowSteps = 80;
	static constexpr int32 MinLossesForPanic = 2;
	static constexpr int32 MinSupportForPanic = 2;
	static constexpr int32 ImmuneAllyWeight = 2;
	static constexpr int32 FearMinSteps = 64;
	static constexpr int32 FearMaxSteps = 128;
	static constexpr int32 LeaderFearMinSteps = 128;
	static constexpr int32 LeaderFearMaxSteps = 192;
	static constexpr int32 FearPendingSteps = 64;
	static constexpr int32 FearResumeMinSteps = 16;
	static constexpr float FearSpeedScale = 1.2f;
	static constexpr float PanicHoldDistanceCm = 700.f;

	static FTDMonsterMoraleFragment MakeFragment(const FTDResolvedMonsterDefinition& Definition);
	static void RecordLoss(FTDMonsterMoraleFragment& Fragment, uint64 StepIndex);
	static int32 CountRecentLosses(const FTDMonsterMoraleFragment& Fragment, uint64 StepIndex);
	static void ClearLosses(FTDMonsterMoraleFragment& Fragment);
	static bool ShouldPanic(int32 RecentLossCount, int32 PanickingAllyCount, int32 AllySupport);
	static int32 ComputeFearSteps(uint32 SimulationId, uint64 StepIndex, bool bIsLeaderShock);
	static int32 GetAllyWeight(bool bIsImmune);
	static bool IsInFear(const FTDMonsterMoraleFragment& Fragment);
};
