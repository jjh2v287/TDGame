#include "MonsterAI/TDPackAlarm.h"

int32 FTDPackAlarm::ComputeHopDelaySteps(const int32 DistanceCm)
{
	return AlarmHopBaseDelaySteps + AlarmHopDelaySpanSteps * FMath::Clamp(DistanceCm, 0, AlarmHopRangeCm) / AlarmHopRangeCm;
}

bool FTDPackAlarm::IsAlarmDueBefore(const FTDPendingAlarm& Left, const FTDPendingAlarm& Right)
{
	if (Left.DueStep != Right.DueStep)
	{
		return Left.DueStep < Right.DueStep;
	}
	return Left.SimulationId < Right.SimulationId;
}

void FTDPackAlarm::AddOrKeepEarlier(TArray<FTDPendingAlarm>& Alarms, const FTDPendingAlarm& Alarm)
{
	for (FTDPendingAlarm& Existing : Alarms)
	{
		if (Existing.SlotIndex != Alarm.SlotIndex || Existing.Generation != Alarm.Generation)
		{
			continue;
		}
		const bool bIsEarlier = Alarm.DueStep < Existing.DueStep;
		const bool bIsSameDueWithLowerHop = Alarm.DueStep == Existing.DueStep && Alarm.Hop < Existing.Hop;
		if (bIsEarlier || bIsSameDueWithLowerHop)
		{
			Existing = Alarm;
		}
		return;
	}
	Alarms.Add(Alarm);
}

void FTDPackAlarm::RemoveSlot(TArray<FTDPendingAlarm>& Alarms, const int32 SlotIndex)
{
	Alarms.RemoveAll([SlotIndex](const FTDPendingAlarm& Alarm)
	{
		return Alarm.SlotIndex == SlotIndex;
	});
}
