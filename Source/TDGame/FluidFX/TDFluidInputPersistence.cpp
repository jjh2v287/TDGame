#include "FluidFX/TDFluidInputPersistence.h"
#include "Algo/Sort.h"

void FTDFluidInputPersistence::RecordEmitted(const FTDFluidInputSourceKey& Key, const FTDFluidBrushStamp& Stamp)
{
	FTDFluidLingeringStamp* Entry = Entries.Find(Key);
	if (!Entry)
	{
		Entry = &Entries.Add(Key);
		Entry->RecordSequence = NextRecordSequence++;
	}

	Entry->Stamp = Stamp;
	Entry->AgeSeconds = 0.0f;
	Entry->bWasRecordedThisStep = true;
}

void FTDFluidInputPersistence::EmitLingering(float DeltaTime, float TauSeconds, TArray<FTDFluidBrushStamp>& OutStamps)
{
	if (TauSeconds <= 0.0f)
	{
		Reset();
		return;
	}

	TArray<const FTDFluidLingeringStamp*> EmittedEntries;
	for (auto EntryIt = Entries.CreateIterator(); EntryIt; ++EntryIt)
	{
		FTDFluidLingeringStamp& Entry = EntryIt.Value();
		if (Entry.bWasRecordedThisStep)
		{
			Entry.bWasRecordedThisStep = false;
			continue;
		}

		Entry.AgeSeconds += FMath::Max(DeltaTime, 0.0f);
		if (ComputeRetainFactor(Entry.AgeSeconds, TauSeconds) < RetainCutoff)
		{
			EntryIt.RemoveCurrent();
			continue;
		}
		EmittedEntries.Add(&Entry);
	}

	Algo::Sort(EmittedEntries, [](const FTDFluidLingeringStamp* Left, const FTDFluidLingeringStamp* Right)
	{
		return Left->RecordSequence < Right->RecordSequence;
	});

	OutStamps.Reserve(OutStamps.Num() + EmittedEntries.Num());
	for (const FTDFluidLingeringStamp* Entry : EmittedEntries)
	{
		const float RetainFactor = ComputeRetainFactor(Entry->AgeSeconds, TauSeconds);
		FTDFluidBrushStamp& Stamp = OutStamps.Add_GetRef(Entry->Stamp);
		Stamp.DensityStrength *= RetainFactor;
		Stamp.VelocityStrength *= RetainFactor;
		Stamp.RadialVelocityCmPerSec *= RetainFactor;
		Stamp.HeatStrength *= RetainFactor;
		Stamp.RippleImpulseCm = 0.0f;
	}
}

void FTDFluidInputPersistence::Reset()
{
	Entries.Reset();
	NextRecordSequence = 0;
}

float FTDFluidInputPersistence::ComputeRetainFactor(float AgeSeconds, float TauSeconds)
{
	if (TauSeconds <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Exp(-FMath::Max(AgeSeconds, 0.0f) / TauSeconds);
}
