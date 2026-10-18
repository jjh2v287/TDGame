#pragma once

#include "CoreMinimal.h"
#include "MonsterAI/TDCombatDirector.h"
#include "MonsterAI/TDMonsterDefinition.h"

enum class ETDTacticsTokenState : uint8
{
	None,
	Reserved,
	Committed
};

enum class ETDTacticsRole : uint8
{
	Melee,
	Ranged,
	Boss,
	Charger,
	Support
};

enum class ETDTacticsSlotKind : uint8
{
	None,
	Wait,
	Inner,
	Ranged
};

struct FTDMonsterTacticsFragment
{
	ETDTacticsTokenState TokenState = ETDTacticsTokenState::None;
	uint32 TokenRecordId = 0;
	uint32 TargetRecordId = 0;
	uint64 GrantStep = 0;
	uint64 RegrantReadyStep = 0;
	uint64 LastAttackEndStep = 0;
	uint64 LastDamagedStep = 0;
	ETDTacticsRole Role = ETDTacticsRole::Melee;
	int32 MeleeReachCm = 0;
	int32 MaxReachCm = 0;
	int32 ReserveCostQuarters = TDMonsterAI::QuartersPerToken;
	int32 CommittedCostQuarters = 0;
	int8 WaitRing = -1;
	int8 WaitSector = -1;
	int8 InnerSector = -1;
	int8 RangedSector = -1;
	bool bIsOnDeck = false;
	uint64 SlotProgressAnchorStep = 0;
	int32 SlotProgressBestCm = 0;
	uint64 ReservationProgressAnchorStep = 0;
	int32 ReservationBestDistanceCm = 0;
	uint64 StalledUntilStep = 0;
	uint32 LastCastStartCount = 0;
};

struct FTDTacticsPool
{
	int32 EngagedCount = 0;
	int32 EngagedCostQuarters = 0;
	int32 BaseTokens = 0;
	int32 BudgetQuarters = 0;
	int32 HeldQuarters = 0;
	int32 LowerPassCount = 0;
	int32 RoleBlockedCount = 0;
};

struct FTDTargetTacticsRecord
{
	static constexpr int32 WaitRingCount = 2;
	static constexpr int32 MaxWaitSectorCount = 16;
	static constexpr int32 InnerSectorCount = 8;
	static constexpr int32 RangedSectorCount = 12;
	static constexpr int32 MeleePoolIndex = 0;
	static constexpr int32 RangedPoolIndex = 1;

	uint32 RecordId = 0;
	TWeakObjectPtr<AActor> Target;
	uint32 WaitOccupants[WaitRingCount][MaxWaitSectorCount] = {};
	uint32 InnerOccupants[InnerSectorCount] = {};
	uint32 RangedOccupants[RangedSectorCount] = {};
	uint64 WaitBlockedUntilStep[WaitRingCount][MaxWaitSectorCount] = {};
	uint64 InnerBlockedUntilStep[InnerSectorCount] = {};
	uint64 RangedBlockedUntilStep[RangedSectorCount] = {};
	FTDTacticsPool Pools[2];
	uint64 LastAttackStartStep = 0;
	uint64 LastBossStartStep = 0;
	uint64 OpeningGraceUntilStep = 0;
	uint64 GrantFreezeUntilStep = 0;
	uint64 ZeroEngagedSinceStep = 0;
	FTDCombatDirectorState Director;
	int64 PrevHealthCenti = 0;
	int32 PreviousTargetXCm = 0;
	int32 PreviousTargetYCm = 0;
	uint64 PreviousTargetStep = 0;
	int32 RangedBandFloorCm = 0;
	TArray<int32> MemberSlots;

	int32 GetEngagedCount() const
	{
		return Pools[MeleePoolIndex].EngagedCount + Pools[RangedPoolIndex].EngagedCount;
	}

	bool HasMeleeQueue() const
	{
		return Pools[MeleePoolIndex].EngagedCostQuarters > Pools[MeleePoolIndex].BudgetQuarters || Pools[MeleePoolIndex].RoleBlockedCount > 0;
	}
};

struct FTDSectorTableView
{
	uint32* Occupants = nullptr;
	uint64* BlockedUntilStep = nullptr;
	int32 SectorCount = 0;
	int32 OffsetDecidegrees = 0;
};

struct FTDSectorSearch
{
	int32 BearingDecidegrees = 0;
	int32 Width = 1;
	uint64 StepIndex = 0;
	bool bHasMovingTarget = false;
	int32 MovingDecidegrees = 0;
	bool bHasSouthPenalty = false;
};

struct FTDTacticsCandidateKey
{
	bool bIsRetaliating = false;
	bool bIsRegrantReady = false;
	int32 WaitStage = 0;
	int64 DistanceSquared = 0;
	uint32 SimulationId = 0;
	int32 SlotIndex = INDEX_NONE;
};

class TDGAME_API FTDCombatTactics
{
public:
	static constexpr int32 TokenPassPeriodSteps = 8;
	static constexpr int32 RingPassPeriodSteps = 32;
	static constexpr int32 MaxTokensPerTarget = 5;
	static constexpr int32 MaxRangedTokens = 3;
	static constexpr int32 ReservationExpirySteps = 256;
	static constexpr int32 RegrantCooldownSteps = 48;
	static constexpr int32 WaitStageCap = 64;
	static constexpr int32 MeleeGrantRadius = 900;
	static constexpr int32 RangedGrantMargin = 100;
	static constexpr int32 EngagedCountRadius = 1600;
	static constexpr int32 RangedRoleRange = 450;
	static constexpr int32 BossSpacingSteps = 24;
	static constexpr int32 RetaliationSteps = 64;
	static constexpr int32 RetaliationSourceRadiusCm = 250;
	static constexpr int32 WaitRingInnerMin = 320;
	static constexpr int32 WaitRingInnerMax = 520;
	static constexpr int32 WaitRingMargin = 150;
	static constexpr int32 WaitRingSpacing = 200;
	static constexpr int32 WaitSectorCounts[FTDTargetTacticsRecord::WaitRingCount] = { 12, 16 };
	static constexpr int32 InnerSectorCount = FTDTargetTacticsRecord::InnerSectorCount;
	static constexpr int32 RangedSectorCount = FTDTargetTacticsRecord::RangedSectorCount;
	static constexpr int32 RangedSectorOffsetDecidegrees = 150;
	static constexpr int32 SlotKeepErrorDecidegrees = 450;
	static constexpr int32 SlotSearchHalfDecidegrees = 900;
	static constexpr int32 SlotGapCm = 60;
	static constexpr int32 BlockedProgressSteps = 64;
	static constexpr int32 BlockedProgressCm = 20;
	static constexpr int32 BlockedAngleProgressDecidegrees = 20;
	static constexpr int32 BlockedArrivalCm = 60;
	static constexpr int32 BlockedSectorSteps = 512;
	static constexpr int32 StalledExclusionSteps = 512;
	static constexpr int32 StallReanchorCm = 60;
	static constexpr int32 MovingTargetSpeed = 150;
	static constexpr int32 MovingTargetPenaltyDecidegrees = 600;
	static constexpr int32 SouthPenaltyDecidegrees = 900;
	static constexpr int32 DriftHalfDecidegrees = 60;
	static constexpr int32 DriftPeriodSteps = 256;
	static constexpr int32 FeintDepthCm = 40;
	static constexpr int32 FeintSteps = 32;
	static constexpr int32 FeintEvery = 6;
	static constexpr int32 EncounterResetSteps = 128;
	static constexpr int32 ReachSlackCm = 500;
	static constexpr int32 WaitArrivalCm = 40;
	static constexpr int32 FeintArrivalCm = 15;
	static constexpr int32 WaitInsideMarginCm = 60;
	static constexpr int32 QueueOuterMarginCm = 200;
	static constexpr int32 MaxChargerHolders = 1;
	static constexpr int32 MaxSupportHolders = 1;
	static constexpr int32 RangedBandGapCm = 100;
	static constexpr int32 RangedReachMarginCm = 100;
	static constexpr int32 RangedBandFloorCapCm = 700;
	static constexpr int32 RangedBandMinWidthCm = 100;

	static FTDMonsterTacticsFragment MakeFragment(const FTDResolvedMonsterDefinition& Definition);
	static bool IsMeleeLaneRole(ETDTacticsRole Role);
	static bool IsRangedLaneRole(ETDTacticsRole Role);
	static int32 GetExclusiveRoleCap(ETDTacticsRole Role);
	static int32 ComputeRangedBandFloor(int32 RecordFloorCm, int32 BodyRadiusCm, int32 MaxReachCm);
	static int32 GetAbilityCostQuarters(const FTDResolvedMonsterDefinition& Definition, int32 AbilityIndex);
	static int32 GetHeldCostQuarters(const FTDMonsterTacticsFragment& Fragment);
	static void CommitToken(FTDMonsterTacticsFragment& Fragment, int32 CastCostQuarters);
	static uint64 GetReferencedAbilityMask(const FTDResolvedMonsterDefinition& Definition);
	static int32 ComputeBaseTokens(int32 EngagedCount, int32 TokenCap);
	static void ComputePoolBaseTokens(int32 MeleeCount, int32 RangedCount, int32& OutMeleeTokens, int32& OutRangedTokens);
	static int32 ComputePoolTokens(int32 BaseTokens, int32 BudgetPercent);
	static int32 ComputeWaitStage(uint64 StepIndex, uint64 LastAttackEndStep);
	static bool IsCandidateBefore(const FTDTacticsCandidateKey& Left, const FTDTacticsCandidateKey& Right);
	static int32 ToDecidegrees(double X, double Y);
	static int32 GetAngleDifference(int32 Left, int32 Right);
	static bool IsSouthDecidegrees(int32 Decidegrees);
	static FVector2D MakeDirection(int32 Decidegrees);
	static int32 GetSectorCenter(int32 SectorCount, int32 OffsetDecidegrees, int32 Sector);
	static int32 ComputeSectorWidth(int32 RadiusCm, int32 SectorCount, int32 BodyRadiusCm);
	static float ComputeSectorHalfAngleDegrees(int32 SectorCount, int32 Width);
	static int32 ComputeWaitRingRadius(int32 MeleeReachCm, int32 BodyRadiusCm, int32 Ring);
	static int32 ComputeDriftDecidegrees(uint32 SimulationId, uint64 StepIndex);
	static bool IsFeinting(uint32 SimulationId, uint64 StepIndex);
	static int64 ComputePlanarDistanceSquared(const FVector& Left, const FVector& Right);
	static bool IsSpanFree(const FTDSectorTableView& Table, int32 Center, int32 Width, uint64 StepIndex);
	static bool CanKeepSector(const FTDSectorTableView& Table, int32 Sector, const FTDSectorSearch& Search);
	static int32 FindBestSector(const FTDSectorTableView& Table, const FTDSectorSearch& Search);
	static void ClaimSpan(const FTDSectorTableView& Table, int32 Center, int32 Width, uint32 SimulationId);
	static void ReleaseOccupant(const FTDSectorTableView& Table, uint32 SimulationId);
	static void ClearOccupants(const FTDSectorTableView& Table);
	static FTDSectorTableView MakeWaitTable(FTDTargetTacticsRecord& Record, int32 Ring);
	static FTDSectorTableView MakeInnerTable(FTDTargetTacticsRecord& Record);
	static FTDSectorTableView MakeRangedTable(FTDTargetTacticsRecord& Record);

private:
	static int32 GetSpanCell(const FTDSectorTableView& Table, int32 Center, int32 Width, int32 Offset);
};
