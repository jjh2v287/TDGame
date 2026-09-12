#include "Dungeon/TDEncounterDefinitions.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	void CollectAffordableEntries(const UTDEncounterSet& EncounterSet, int32 RoomDepth, float RemainingBudget, TArray<int32>& OutEntryIndices)
	{
		OutEntryIndices.Reset();
		for (int32 Index = 0; Index < EncounterSet.Entries.Num(); ++Index)
		{
			const FTDEncounterEntry& Entry = EncounterSet.Entries[Index];
			if (!Entry.MatchesDepth(RoomDepth) || Entry.Weight <= 0.0f || Entry.EnemyClass.IsNull())
			{
				continue;
			}
			if (Entry.DifficultyCost > RemainingBudget + KINDA_SMALL_NUMBER)
			{
				continue;
			}
			OutEntryIndices.Add(Index);
		}
	}

	int32 RollWeightedIndex(const UTDEncounterSet& EncounterSet, const TArray<int32>& EntryIndices, FRandomStream& Stream)
	{
		float TotalWeight = 0.0f;
		for (int32 EntryIndex : EntryIndices)
		{
			TotalWeight += EncounterSet.Entries[EntryIndex].Weight;
		}
		float Roll = Stream.FRandRange(0.0f, TotalWeight);
		for (int32 EntryIndex : EntryIndices)
		{
			Roll -= EncounterSet.Entries[EntryIndex].Weight;
			if (Roll <= 0.0f)
			{
				return EntryIndex;
			}
		}
		return EntryIndices.Last();
	}
}

TArray<FTDEncounterPick> FTDEncounterResolver::Resolve(const UTDEncounterSet* EncounterSet, int32 RoomDepth, int32 MarkerCount, const FTDSeedContext& Seed)
{
	TArray<FTDEncounterPick> Picks;
	if (!EncounterSet || MarkerCount <= 0)
	{
		return Picks;
	}
	const int32 SlotCount = FMath::Min(MarkerCount, FMath::Max(0, EncounterSet->MaxPerRoom));
	float RemainingBudget = EncounterSet->DifficultyBudgetPerRoom;
	FRandomStream Stream = Seed.Derive(TEXT("Encounter"), RoomDepth);
	TArray<int32> Affordable;
	for (int32 MarkerIndex = 0; MarkerIndex < SlotCount; ++MarkerIndex)
	{
		CollectAffordableEntries(*EncounterSet, RoomDepth, RemainingBudget, Affordable);
		if (Affordable.Num() == 0)
		{
			break;
		}
		const int32 EntryIndex = RollWeightedIndex(*EncounterSet, Affordable, Stream);
		const FTDEncounterEntry& Entry = EncounterSet->Entries[EntryIndex];
		FTDEncounterPick& Pick = Picks.AddDefaulted_GetRef();
		Pick.MarkerIndex = MarkerIndex;
		Pick.EntryIndex = EntryIndex;
		Pick.EnemyClass = Entry.EnemyClass;
		Pick.DifficultyCost = Entry.DifficultyCost;
		RemainingBudget -= Entry.DifficultyCost;
	}
	return Picks;
}

float FTDEncounterResolver::SumDifficulty(const TArray<FTDEncounterPick>& Picks)
{
	float Total = 0.0f;
	for (const FTDEncounterPick& Pick : Picks)
	{
		Total += Pick.DifficultyCost;
	}
	return Total;
}

#if WITH_EDITOR
EDataValidationResult UTDEncounterSet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Entries.Num() == 0)
	{
		Context.AddError(FText::FromString(TEXT("인카운터 항목이 하나도 없습니다")));
		return EDataValidationResult::Invalid;
	}
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FTDEncounterEntry& Entry = Entries[Index];
		if (Entry.EnemyClass.IsNull())
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("항목 %d: EnemyClass가 비어 있습니다"), Index)));
			Result = EDataValidationResult::Invalid;
		}
		if (Entry.MinDepth > Entry.MaxDepth)
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("항목 %d: MinDepth(%d) > MaxDepth(%d)"), Index, Entry.MinDepth, Entry.MaxDepth)));
			Result = EDataValidationResult::Invalid;
		}
		if (Entry.DifficultyCost > DifficultyBudgetPerRoom)
		{
			Context.AddWarning(FText::FromString(FString::Printf(TEXT("항목 %d: DifficultyCost(%.1f)가 방 예산(%.1f)을 넘어 절대 선택되지 않습니다"), Index, Entry.DifficultyCost, DifficultyBudgetPerRoom)));
		}
	}
	if (MaxPerRoom <= 0)
	{
		Context.AddWarning(FText::FromString(TEXT("MaxPerRoom이 0이라 아무것도 스폰되지 않습니다")));
	}
	return Result;
}
#endif
