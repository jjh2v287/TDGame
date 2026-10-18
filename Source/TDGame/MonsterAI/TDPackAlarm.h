#pragma once

#include "CoreMinimal.h"

class AActor;

struct FTDPendingAlarm
{
	int32 SlotIndex = INDEX_NONE;
	uint32 Generation = 0;
	uint32 SimulationId = 0;
	TWeakObjectPtr<AActor> Target;
	uint64 DueStep = 0;
	uint8 Hop = 0;
};

struct FTDPackDeathAlarm
{
	FVector Location = FVector::ZeroVector;
	TWeakObjectPtr<AActor> Target;
	int32 TeamId = 0;
	uint32 SimulationId = 0;
};

class TDGAME_API FTDPackAlarm
{
public:
	static constexpr int32 MaxAlarmHops = 2;
	static constexpr int32 AlarmHopBaseDelaySteps = 8;
	static constexpr int32 AlarmHopDelaySpanSteps = 8;
	static constexpr int32 AlarmHopRangeCm = 900;
	static constexpr int32 AlarmCallMemorySteps = 192;
	static constexpr int32 AlarmCallRadiusCm = 1800;

	static int32 ComputeHopDelaySteps(int32 DistanceCm);
	static bool IsAlarmDueBefore(const FTDPendingAlarm& Left, const FTDPendingAlarm& Right);
	static void AddOrKeepEarlier(TArray<FTDPendingAlarm>& Alarms, const FTDPendingAlarm& Alarm);
	static void RemoveSlot(TArray<FTDPendingAlarm>& Alarms, int32 SlotIndex);
};
