#include "Combat/Feedback/TDDamageNumberPool.h"

void FTDDamageNumberPool::Configure(const FTDDamageNumberPoolSettings& InSettings)
{
	Settings = InSettings;
	Settings.Capacity = FMath::Max(1, Settings.Capacity);
	Settings.LifetimeSeconds = FMath::Max(0.05f, Settings.LifetimeSeconds);
	Entries.Reset();
	Entries.SetNum(Settings.Capacity);
	ScatterRandom.Initialize(Settings.RandomSeed);
	NextSerial = 1;
}

int32 FTDDamageNumberPool::Add(const FTDDamageNumberRequest& Request, const double Now)
{
	if (Entries.IsEmpty())
	{
		return INDEX_NONE;
	}

	const int32 MergeIndex = FindStatusMergeIndex(Request, Now);
	if (MergeIndex != INDEX_NONE)
	{
		FTDDamageNumberEntry& MergedEntry = Entries[MergeIndex];
		MergedEntry.Amount += Request.Amount;
		MergedEntry.Serial = NextSerial++;
		return MergeIndex;
	}

	const bool bIsStatus = Request.Style == ETDDamageNumberStyle::Status;
	const int32 StackIndex = bIsStatus ? 0 : CountRecentHitsOnTarget(Request.TargetKey, Now);
	const int32 Index = FindFreeOrOldestIndex();
	FTDDamageNumberEntry& Entry = Entries[Index];
	Entry.WorldLocation = Request.WorldLocation;
	Entry.TargetKey = Request.TargetKey;
	Entry.StartTime = Now;
	Entry.Amount = Request.Amount;
	Entry.ScatterX = static_cast<float>(ScatterRandom.FRandRange(-Settings.ScatterRange, Settings.ScatterRange));
	Entry.StackY = -static_cast<float>(StackIndex) * Settings.StackOffset;
	Entry.Serial = NextSerial++;
	Entry.Style = Request.Style;
	Entry.Element = Request.Element;
	Entry.bIsPlayerTarget = Request.bIsPlayerTarget;
	Entry.bWasCritical = Request.bWasCritical;
	Entry.bIsActive = true;
	return Index;
}

bool FTDDamageNumberPool::CanMergeStatus(const FTDDamageNumberRequest& Request, const double Now) const
{
	return FindStatusMergeIndex(Request, Now) != INDEX_NONE;
}

void FTDDamageNumberPool::ExpireFinished(const double Now)
{
	for (FTDDamageNumberEntry& Entry : Entries)
	{
		if (Entry.bIsActive && Now - Entry.StartTime >= Settings.LifetimeSeconds)
		{
			Entry.bIsActive = false;
		}
	}
}

bool FTDDamageNumberPool::ComputeVisual(const int32 Index, const double Now, FTDDamageNumberVisual& OutVisual) const
{
	if (!Entries.IsValidIndex(Index))
	{
		return false;
	}

	const FTDDamageNumberEntry& Entry = Entries[Index];
	if (!Entry.bIsActive)
	{
		return false;
	}

	const float Elapsed = FMath::Max(0.f, static_cast<float>(Now - Entry.StartTime));
	OutVisual.WorldLocation = Entry.WorldLocation + FVector(0.f, 0.f, Settings.RiseCm * ComputeRiseFraction(Elapsed, Settings.LifetimeSeconds));
	OutVisual.ScreenOffset = FVector2D(Entry.ScatterX, Entry.StackY);
	OutVisual.Scale = ComputePopScale(Elapsed, Settings.PopSeconds, Settings.PopStartScale);
	OutVisual.Opacity = ComputeOpacity(Elapsed, Settings.LifetimeSeconds, Settings.FadeFraction);
	return true;
}

int32 FTDDamageNumberPool::CountActive() const
{
	int32 ActiveCount = 0;
	for (const FTDDamageNumberEntry& Entry : Entries)
	{
		ActiveCount += Entry.bIsActive ? 1 : 0;
	}
	return ActiveCount;
}

float FTDDamageNumberPool::ComputePopScale(const float ElapsedSeconds, const float PopSeconds, const float StartScale)
{
	if (PopSeconds <= 0.f)
	{
		return 1.f;
	}
	const float Alpha = FMath::Clamp(ElapsedSeconds / PopSeconds, 0.f, 1.f);
	return FMath::Lerp(StartScale, 1.f, Alpha);
}

float FTDDamageNumberPool::ComputeOpacity(const float ElapsedSeconds, const float LifetimeSeconds, const float FadeFraction)
{
	if (ElapsedSeconds >= LifetimeSeconds)
	{
		return 0.f;
	}
	const float FadeSeconds = LifetimeSeconds * FMath::Clamp(FadeFraction, 0.f, 1.f);
	if (FadeSeconds <= 0.f)
	{
		return 1.f;
	}
	const float FadeStart = LifetimeSeconds - FadeSeconds;
	return 1.f - FMath::Clamp((ElapsedSeconds - FadeStart) / FadeSeconds, 0.f, 1.f);
}

float FTDDamageNumberPool::ComputeRiseFraction(const float ElapsedSeconds, const float LifetimeSeconds)
{
	if (LifetimeSeconds <= 0.f)
	{
		return 1.f;
	}
	const float Progress = FMath::Clamp(ElapsedSeconds / LifetimeSeconds, 0.f, 1.f);
	return 1.f - FMath::Square(1.f - Progress);
}

int32 FTDDamageNumberPool::FindStatusMergeIndex(const FTDDamageNumberRequest& Request, const double Now) const
{
	if (Request.Style != ETDDamageNumberStyle::Status)
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FTDDamageNumberEntry& Entry = Entries[Index];
		if (!Entry.bIsActive || Entry.Style != ETDDamageNumberStyle::Status || Entry.TargetKey != Request.TargetKey)
		{
			continue;
		}
		if (Now - Entry.StartTime <= Settings.StatusMergeSeconds)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 FTDDamageNumberPool::FindFreeOrOldestIndex() const
{
	int32 OldestIndex = 0;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FTDDamageNumberEntry& Entry = Entries[Index];
		if (!Entry.bIsActive)
		{
			return Index;
		}
		if (Entry.StartTime < Entries[OldestIndex].StartTime)
		{
			OldestIndex = Index;
		}
	}
	return OldestIndex;
}

int32 FTDDamageNumberPool::CountRecentHitsOnTarget(const FObjectKey& TargetKey, const double Now) const
{
	int32 RecentCount = 0;
	for (const FTDDamageNumberEntry& Entry : Entries)
	{
		if (!Entry.bIsActive || Entry.Style == ETDDamageNumberStyle::Status || Entry.TargetKey != TargetKey)
		{
			continue;
		}
		RecentCount += Now - Entry.StartTime <= Settings.StackWindowSeconds ? 1 : 0;
	}
	return RecentCount;
}
