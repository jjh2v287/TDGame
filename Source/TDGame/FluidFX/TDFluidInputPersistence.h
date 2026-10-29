#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"
#include "UObject/ObjectKey.h"

struct FTDFluidInputSourceKey
{
	FObjectKey Object;
	FName Name = NAME_None;
	int32 Index = INDEX_NONE;

	bool operator==(const FTDFluidInputSourceKey& Other) const
	{
		return Object == Other.Object && Name == Other.Name && Index == Other.Index;
	}

	friend uint32 GetTypeHash(const FTDFluidInputSourceKey& Key)
	{
		return HashCombine(HashCombine(GetTypeHash(Key.Object), GetTypeHash(Key.Name)), ::GetTypeHash(Key.Index));
	}
};

class TDGAME_API FTDFluidInputPersistence
{
public:
	static constexpr float RetainCutoff = 1.0f / 32.0f;

	void RecordEmitted(const FTDFluidInputSourceKey& Key, const FTDFluidBrushStamp& Stamp);
	void EmitLingering(float DeltaTime, float TauSeconds, TArray<FTDFluidBrushStamp>& OutStamps);
	void Reset();
	int32 GetLingeringCount() const { return Entries.Num(); }
	static float ComputeRetainFactor(float AgeSeconds, float TauSeconds);

private:
	struct FTDFluidLingeringStamp
	{
		FTDFluidBrushStamp Stamp;
		float AgeSeconds = 0.0f;
		uint64 RecordSequence = 0;
		bool bWasRecordedThisStep = false;
	};

	TMap<FTDFluidInputSourceKey, FTDFluidLingeringStamp> Entries;
	uint64 NextRecordSequence = 0;
};
