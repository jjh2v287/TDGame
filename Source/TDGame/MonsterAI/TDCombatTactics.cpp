#include "MonsterAI/TDCombatTactics.h"
#include "MonsterAI/TDMonsterDefinition.h"

namespace
{
	void AddReferencedAbility(const FTDResolvedMonsterDefinition& Definition, const int32 AbilityIndex, uint64& InOutMask)
	{
		if (!Definition.Abilities.IsValidIndex(AbilityIndex) || AbilityIndex >= 64)
		{
			return;
		}
		InOutMask |= uint64(1) << AbilityIndex;
	}

	void AddSequenceAbilities(const FTDResolvedMonsterDefinition& Definition, const int32 SequenceIndex, uint64& InOutMask)
	{
		if (!Definition.Sequences.IsValidIndex(SequenceIndex))
		{
			return;
		}
		for (const FTDResolvedStep& Step : Definition.Sequences[SequenceIndex])
		{
			if (Step.Primitive == ETDMonsterPrimitive::CastAbility)
			{
				AddReferencedAbility(Definition, Step.Args.AbilityIndex, InOutMask);
			}
		}
	}
}

FTDMonsterTacticsFragment FTDCombatTactics::MakeFragment(const FTDResolvedMonsterDefinition& Definition)
{
	FTDMonsterTacticsFragment Fragment;
	const uint64 ReferencedMask = GetReferencedAbilityMask(Definition);
	bool bHasMeleeAbility = false;
	bool bHasAnyAbility = false;
	int32 MaxCostQuarters = 0;
	for (int32 AbilityIndex = 0; AbilityIndex < Definition.Abilities.Num() && AbilityIndex < 64; ++AbilityIndex)
	{
		if ((ReferencedMask & (uint64(1) << AbilityIndex)) == 0)
		{
			continue;
		}
		const int32 RangeCm = FMath::FloorToInt32(Definition.Abilities[AbilityIndex].Range);
		bHasAnyAbility = true;
		MaxCostQuarters = FMath::Max(MaxCostQuarters, Definition.Abilities[AbilityIndex].TokenCostQuarters);
		Fragment.MaxReachCm = FMath::Max(Fragment.MaxReachCm, RangeCm);
		if (RangeCm < RangedRoleRange)
		{
			bHasMeleeAbility = true;
			Fragment.MeleeReachCm = FMath::Max(Fragment.MeleeReachCm, RangeCm);
		}
	}
	Fragment.ReserveCostQuarters = MaxCostQuarters > 0 ? MaxCostQuarters : TDMonsterAI::QuartersPerToken;
	if (Definition.bIsBoss)
	{
		Fragment.Role = ETDTacticsRole::Boss;
		return Fragment;
	}
	if (Definition.CombatRole == ETDMonsterCombatRole::Charger)
	{
		Fragment.Role = ETDTacticsRole::Charger;
		return Fragment;
	}
	if (Definition.CombatRole == ETDMonsterCombatRole::Support)
	{
		Fragment.Role = ETDTacticsRole::Support;
		return Fragment;
	}
	if (Definition.CombatRole == ETDMonsterCombatRole::Melee)
	{
		Fragment.Role = ETDTacticsRole::Melee;
		return Fragment;
	}
	if (Definition.CombatRole == ETDMonsterCombatRole::Ranged)
	{
		Fragment.Role = ETDTacticsRole::Ranged;
		return Fragment;
	}
	Fragment.Role = bHasAnyAbility && !bHasMeleeAbility ? ETDTacticsRole::Ranged : ETDTacticsRole::Melee;
	return Fragment;
}

bool FTDCombatTactics::IsMeleeLaneRole(const ETDTacticsRole Role)
{
	return Role == ETDTacticsRole::Melee || Role == ETDTacticsRole::Charger;
}

bool FTDCombatTactics::IsRangedLaneRole(const ETDTacticsRole Role)
{
	return Role == ETDTacticsRole::Ranged || Role == ETDTacticsRole::Support;
}

int32 FTDCombatTactics::GetExclusiveRoleCap(const ETDTacticsRole Role)
{
	if (Role == ETDTacticsRole::Charger)
	{
		return MaxChargerHolders;
	}
	if (Role == ETDTacticsRole::Support)
	{
		return MaxSupportHolders;
	}
	return MAX_int32;
}

int32 FTDCombatTactics::ComputeRangedBandFloor(const int32 RecordFloorCm, const int32 BodyRadiusCm, const int32 MaxReachCm)
{
	if (RecordFloorCm <= 0)
	{
		return 0;
	}
	const int32 CeilingCm = FMath::Max(0, FMath::Min(MaxReachCm - RangedReachMarginCm, RangedBandFloorCapCm));
	return FMath::Clamp(RecordFloorCm + RangedBandGapCm + BodyRadiusCm, 0, CeilingCm);
}

int32 FTDCombatTactics::GetAbilityCostQuarters(const FTDResolvedMonsterDefinition& Definition, const int32 AbilityIndex)
{
	if (!Definition.Abilities.IsValidIndex(AbilityIndex))
	{
		return TDMonsterAI::QuartersPerToken;
	}
	return Definition.Abilities[AbilityIndex].TokenCostQuarters;
}

int32 FTDCombatTactics::GetHeldCostQuarters(const FTDMonsterTacticsFragment& Fragment)
{
	if (Fragment.TokenState == ETDTacticsTokenState::Reserved)
	{
		return Fragment.ReserveCostQuarters;
	}
	if (Fragment.TokenState == ETDTacticsTokenState::Committed)
	{
		return Fragment.CommittedCostQuarters > 0 ? Fragment.CommittedCostQuarters : Fragment.ReserveCostQuarters;
	}
	return 0;
}

void FTDCombatTactics::CommitToken(FTDMonsterTacticsFragment& Fragment, const int32 CastCostQuarters)
{
	if (Fragment.TokenState == ETDTacticsTokenState::None)
	{
		return;
	}
	const int32 SettledQuarters = FMath::Clamp(CastCostQuarters, 1, Fragment.ReserveCostQuarters);
	if (Fragment.TokenState == ETDTacticsTokenState::Reserved)
	{
		Fragment.TokenState = ETDTacticsTokenState::Committed;
		Fragment.CommittedCostQuarters = SettledQuarters;
		return;
	}
	Fragment.CommittedCostQuarters = FMath::Max(Fragment.CommittedCostQuarters, SettledQuarters);
}

uint64 FTDCombatTactics::GetReferencedAbilityMask(const FTDResolvedMonsterDefinition& Definition)
{
	uint64 Mask = 0;
	for (const TArray<FTDResolvedAction>& Actions : Definition.ActionsByPhase)
	{
		for (const FTDResolvedAction& Action : Actions)
		{
			if (Action.Primitive == ETDMonsterPrimitive::CastAbility)
			{
				AddReferencedAbility(Definition, Action.Args.AbilityIndex, Mask);
			}
			if (Action.Primitive == ETDMonsterPrimitive::PlaySequence)
			{
				AddSequenceAbilities(Definition, Action.Args.SequenceIndex, Mask);
			}
		}
	}
	return Mask;
}

int32 FTDCombatTactics::ComputeBaseTokens(const int32 EngagedCount, const int32 TokenCap)
{
	if (EngagedCount <= 0)
	{
		return 0;
	}
	return FMath::Clamp(FMath::Max((EngagedCount + 1) / 3, FMath::Min(EngagedCount, 2)), 1, TokenCap);
}

void FTDCombatTactics::ComputePoolBaseTokens(const int32 MeleeCount, const int32 RangedCount, int32& OutMeleeTokens, int32& OutRangedTokens)
{
	const int32 MeleeTokenCap = RangedCount > 0 ? MaxTokensPerTarget - 1 : MaxTokensPerTarget;
	OutMeleeTokens = ComputeBaseTokens(MeleeCount, MeleeTokenCap);
	OutRangedTokens = FMath::Min(ComputeBaseTokens(RangedCount, MaxRangedTokens), MaxTokensPerTarget - OutMeleeTokens);
}

int32 FTDCombatTactics::ComputePoolTokens(const int32 BaseTokens, const int32 BudgetPercent)
{
	if (BaseTokens <= 0)
	{
		return 0;
	}
	const int32 RoundedTokens = (BaseTokens * BudgetPercent + 50) / 100;
	return FMath::Max(1, FMath::Max(BaseTokens - 1, RoundedTokens));
}

int32 FTDCombatTactics::ComputeWaitStage(const uint64 StepIndex, const uint64 LastAttackEndStep)
{
	const uint64 WaitedSteps = StepIndex > LastAttackEndStep ? StepIndex - LastAttackEndStep : 0;
	return static_cast<int32>(FMath::Min<uint64>(WaitedSteps / RingPassPeriodSteps, WaitStageCap));
}

bool FTDCombatTactics::IsCandidateBefore(const FTDTacticsCandidateKey& Left, const FTDTacticsCandidateKey& Right)
{
	if (Left.bIsRetaliating != Right.bIsRetaliating)
	{
		return Left.bIsRetaliating;
	}
	if (Left.bIsRegrantReady != Right.bIsRegrantReady)
	{
		return Left.bIsRegrantReady;
	}
	if (Left.WaitStage != Right.WaitStage)
	{
		return Left.WaitStage > Right.WaitStage;
	}
	if (Left.DistanceSquared != Right.DistanceSquared)
	{
		return Left.DistanceSquared < Right.DistanceSquared;
	}
	return Left.SimulationId < Right.SimulationId;
}

int32 FTDCombatTactics::ToDecidegrees(const double X, const double Y)
{
	int32 Decidegrees = FMath::FloorToInt32(FMath::RadiansToDegrees(FMath::Atan2(Y, X)) * 10.0);
	if (Decidegrees < 0)
	{
		Decidegrees += 3600;
	}
	return Decidegrees >= 3600 ? Decidegrees - 3600 : Decidegrees;
}

int32 FTDCombatTactics::GetAngleDifference(const int32 Left, const int32 Right)
{
	const int32 Difference = FMath::Abs(Left - Right) % 3600;
	return Difference > 1800 ? 3600 - Difference : Difference;
}

bool FTDCombatTactics::IsSouthDecidegrees(const int32 Decidegrees)
{
	return Decidegrees > 1200 && Decidegrees < 2400;
}

FVector2D FTDCombatTactics::MakeDirection(const int32 Decidegrees)
{
	const double Radians = FMath::DegreesToRadians(static_cast<double>(Decidegrees) / 10.0);
	return FVector2D(FMath::Cos(Radians), FMath::Sin(Radians));
}

int32 FTDCombatTactics::GetSectorCenter(const int32 SectorCount, const int32 OffsetDecidegrees, const int32 Sector)
{
	return (OffsetDecidegrees + Sector * 3600 / FMath::Max(1, SectorCount)) % 3600;
}

int32 FTDCombatTactics::ComputeSectorWidth(const int32 RadiusCm, const int32 SectorCount, const int32 BodyRadiusCm)
{
	const int32 ArcCm = FMath::Max(1, FMath::FloorToInt32(2.0 * UE_DOUBLE_PI * RadiusCm / FMath::Max(1, SectorCount)));
	const int32 Width = (2 * BodyRadiusCm + SlotGapCm + ArcCm - 1) / ArcCm;
	return FMath::Clamp(Width, 1, FMath::Max(1, SectorCount));
}

float FTDCombatTactics::ComputeSectorHalfAngleDegrees(const int32 SectorCount, const int32 Width)
{
	return 180.f * static_cast<float>(Width) / static_cast<float>(FMath::Max(1, SectorCount));
}

int32 FTDCombatTactics::ComputeWaitRingRadius(const int32 MeleeReachCm, const int32 BodyRadiusCm, const int32 Ring)
{
	const int32 InnerRadius = FMath::Clamp(MeleeReachCm + WaitRingMargin + BodyRadiusCm, WaitRingInnerMin, WaitRingInnerMax);
	return InnerRadius + Ring * WaitRingSpacing;
}

int32 FTDCombatTactics::ComputeDriftDecidegrees(const uint32 SimulationId, const uint64 StepIndex)
{
	const int32 Phase = static_cast<int32>((static_cast<uint64>(SimulationId) * 37 + StepIndex) % DriftPeriodSteps);
	const int32 Quarter = Phase * 4;
	int32 Wave = Quarter;
	if (Quarter >= DriftPeriodSteps)
	{
		Wave = 2 * DriftPeriodSteps - Quarter;
	}
	if (Quarter >= 3 * DriftPeriodSteps)
	{
		Wave = Quarter - 4 * DriftPeriodSteps;
	}
	const int32 Drift = Wave * DriftHalfDecidegrees / DriftPeriodSteps;
	return SimulationId % 2 == 1 ? -Drift : Drift;
}

bool FTDCombatTactics::IsFeinting(const uint32 SimulationId, const uint64 StepIndex)
{
	return (StepIndex / FeintSteps + SimulationId) % FeintEvery == 0;
}

int64 FTDCombatTactics::ComputePlanarDistanceSquared(const FVector& Left, const FVector& Right)
{
	const int64 DeltaX = FMath::FloorToInt64(Left.X) - FMath::FloorToInt64(Right.X);
	const int64 DeltaY = FMath::FloorToInt64(Left.Y) - FMath::FloorToInt64(Right.Y);
	return DeltaX * DeltaX + DeltaY * DeltaY;
}

int32 FTDCombatTactics::GetSpanCell(const FTDSectorTableView& Table, const int32 Center, const int32 Width, const int32 Offset)
{
	return (Center - (Width - 1) / 2 + Offset + Table.SectorCount) % Table.SectorCount;
}

bool FTDCombatTactics::IsSpanFree(const FTDSectorTableView& Table, const int32 Center, const int32 Width, const uint64 StepIndex)
{
	for (int32 Offset = 0; Offset < Width; ++Offset)
	{
		const int32 Cell = GetSpanCell(Table, Center, Width, Offset);
		if (Table.Occupants[Cell] != 0 || Table.BlockedUntilStep[Cell] > StepIndex)
		{
			return false;
		}
	}
	return true;
}

bool FTDCombatTactics::CanKeepSector(const FTDSectorTableView& Table, const int32 Sector, const FTDSectorSearch& Search)
{
	if (Sector < 0 || Sector >= Table.SectorCount)
	{
		return false;
	}
	const int32 Center = GetSectorCenter(Table.SectorCount, Table.OffsetDecidegrees, Sector);
	if (GetAngleDifference(Search.BearingDecidegrees, Center) > SlotKeepErrorDecidegrees)
	{
		return false;
	}
	return IsSpanFree(Table, Sector, Search.Width, Search.StepIndex);
}

int32 FTDCombatTactics::FindBestSector(const FTDSectorTableView& Table, const FTDSectorSearch& Search)
{
	int32 BestSector = -1;
	int32 BestCost = TNumericLimits<int32>::Max();
	for (int32 Sector = 0; Sector < Table.SectorCount; ++Sector)
	{
		const int32 Center = GetSectorCenter(Table.SectorCount, Table.OffsetDecidegrees, Sector);
		const int32 Error = GetAngleDifference(Search.BearingDecidegrees, Center);
		if (Error > SlotSearchHalfDecidegrees || !IsSpanFree(Table, Sector, Search.Width, Search.StepIndex))
		{
			continue;
		}
		int32 Cost = Error;
		if (Search.bHasMovingTarget && GetAngleDifference(Center, Search.MovingDecidegrees) > 900)
		{
			Cost += MovingTargetPenaltyDecidegrees;
		}
		if (Search.bHasSouthPenalty && IsSouthDecidegrees(Center))
		{
			Cost += SouthPenaltyDecidegrees;
		}
		if (Cost < BestCost)
		{
			BestCost = Cost;
			BestSector = Sector;
		}
	}
	return BestSector;
}

void FTDCombatTactics::ClaimSpan(const FTDSectorTableView& Table, const int32 Center, const int32 Width, const uint32 SimulationId)
{
	for (int32 Offset = 0; Offset < Width; ++Offset)
	{
		Table.Occupants[GetSpanCell(Table, Center, Width, Offset)] = SimulationId;
	}
}

void FTDCombatTactics::ReleaseOccupant(const FTDSectorTableView& Table, const uint32 SimulationId)
{
	for (int32 Cell = 0; Cell < Table.SectorCount; ++Cell)
	{
		if (Table.Occupants[Cell] == SimulationId)
		{
			Table.Occupants[Cell] = 0;
		}
	}
}

void FTDCombatTactics::ClearOccupants(const FTDSectorTableView& Table)
{
	for (int32 Cell = 0; Cell < Table.SectorCount; ++Cell)
	{
		Table.Occupants[Cell] = 0;
	}
}

FTDSectorTableView FTDCombatTactics::MakeWaitTable(FTDTargetTacticsRecord& Record, const int32 Ring)
{
	FTDSectorTableView Table;
	Table.Occupants = Record.WaitOccupants[Ring];
	Table.BlockedUntilStep = Record.WaitBlockedUntilStep[Ring];
	Table.SectorCount = WaitSectorCounts[Ring];
	return Table;
}

FTDSectorTableView FTDCombatTactics::MakeInnerTable(FTDTargetTacticsRecord& Record)
{
	FTDSectorTableView Table;
	Table.Occupants = Record.InnerOccupants;
	Table.BlockedUntilStep = Record.InnerBlockedUntilStep;
	Table.SectorCount = InnerSectorCount;
	return Table;
}

FTDSectorTableView FTDCombatTactics::MakeRangedTable(FTDTargetTacticsRecord& Record)
{
	FTDSectorTableView Table;
	Table.Occupants = Record.RangedOccupants;
	Table.BlockedUntilStep = Record.RangedBlockedUntilStep;
	Table.SectorCount = RangedSectorCount;
	Table.OffsetDecidegrees = RangedSectorOffsetDecidegrees;
	return Table;
}
