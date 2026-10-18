#pragma once

#include "CoreMinimal.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Math/RandomStream.h"
#include "UObject/ObjectKey.h"

enum class ETDDamageNumberStyle : uint8
{
	Normal,
	Critical,
	Kill,
	Status,
	PlayerDamage,
	Avoided
};

struct TDGAME_API FTDDamageNumberPoolSettings
{
	int32 Capacity = 48;
	float LifetimeSeconds = 0.9f;
	float RiseCm = 70.f;
	float PopSeconds = 0.12f;
	float PopStartScale = 1.35f;
	float FadeFraction = 0.4f;
	float StatusMergeSeconds = 0.3f;
	float StackWindowSeconds = 0.06f;
	float StackOffset = 20.f;
	float ScatterRange = 16.f;
	int32 RandomSeed = 0x7D1A;
};

struct TDGAME_API FTDDamageNumberRequest
{
	FVector WorldLocation = FVector::ZeroVector;
	FObjectKey TargetKey;
	float Amount = 0.f;
	ETDDamageNumberStyle Style = ETDDamageNumberStyle::Normal;
	ETDDamageElement Element = ETDDamageElement::Physical;
	bool bIsPlayerTarget = false;
	bool bWasCritical = false;
};

struct TDGAME_API FTDDamageNumberEntry
{
	FVector WorldLocation = FVector::ZeroVector;
	FObjectKey TargetKey;
	double StartTime = 0.;
	float Amount = 0.f;
	float ScatterX = 0.f;
	float StackY = 0.f;
	uint32 Serial = 0;
	ETDDamageNumberStyle Style = ETDDamageNumberStyle::Normal;
	ETDDamageElement Element = ETDDamageElement::Physical;
	bool bIsPlayerTarget = false;
	bool bWasCritical = false;
	bool bIsActive = false;
};

struct TDGAME_API FTDDamageNumberVisual
{
	FVector WorldLocation = FVector::ZeroVector;
	FVector2D ScreenOffset = FVector2D::ZeroVector;
	float Scale = 1.f;
	float Opacity = 1.f;
};

class TDGAME_API FTDDamageNumberPool
{
public:
	void Configure(const FTDDamageNumberPoolSettings& InSettings);
	int32 Add(const FTDDamageNumberRequest& Request, double Now);
	bool CanMergeStatus(const FTDDamageNumberRequest& Request, double Now) const;
	void ExpireFinished(double Now);
	bool ComputeVisual(int32 Index, double Now, FTDDamageNumberVisual& OutVisual) const;

	const FTDDamageNumberEntry& GetEntry(int32 Index) const { return Entries[Index]; }
	int32 GetCapacity() const { return Entries.Num(); }
	int32 CountActive() const;
	const FTDDamageNumberPoolSettings& GetSettings() const { return Settings; }

	static float ComputePopScale(float ElapsedSeconds, float PopSeconds, float StartScale);
	static float ComputeOpacity(float ElapsedSeconds, float LifetimeSeconds, float FadeFraction);
	static float ComputeRiseFraction(float ElapsedSeconds, float LifetimeSeconds);

private:
	int32 FindStatusMergeIndex(const FTDDamageNumberRequest& Request, double Now) const;
	int32 FindFreeOrOldestIndex() const;
	int32 CountRecentHitsOnTarget(const FObjectKey& TargetKey, double Now) const;

	TArray<FTDDamageNumberEntry> Entries;
	FTDDamageNumberPoolSettings Settings;
	FRandomStream ScatterRandom{ 0x7D1A };
	uint32 NextSerial = 1;
};
