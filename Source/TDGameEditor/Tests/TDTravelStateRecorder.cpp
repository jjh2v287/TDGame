#include "Tests/TDTravelStateRecorder.h"

namespace
{
	const TCHAR* TravelStateToText(ETDSeamlessTravelState State)
	{
		switch (State)
		{
		case ETDSeamlessTravelState::Idle: return TEXT("Idle");
		case ETDSeamlessTravelState::Preloading: return TEXT("Preloading");
		case ETDSeamlessTravelState::ReadyToTravel: return TEXT("ReadyToTravel");
		case ETDSeamlessTravelState::Traveling: return TEXT("Traveling");
		case ETDSeamlessTravelState::Settling: return TEXT("Settling");
		}
		return TEXT("Unknown");
	}
}

void UTDTravelStateRecorder::BindTo(UTDSeamlessTravelSubsystem* Travel)
{
	if (!Travel)
	{
		return;
	}

	InitialState = Travel->GetTravelState();
	Transitions.Reset();
	Travel->OnTravelStateChanged.AddUniqueDynamic(this, &UTDTravelStateRecorder::HandleTravelStateChanged);
}

void UTDTravelStateRecorder::UnbindFrom(UTDSeamlessTravelSubsystem* Travel)
{
	if (!Travel)
	{
		return;
	}

	Travel->OnTravelStateChanged.RemoveDynamic(this, &UTDTravelStateRecorder::HandleTravelStateChanged);
}

void UTDTravelStateRecorder::HandleTravelStateChanged(ETDSeamlessTravelState OldState, ETDSeamlessTravelState NewState)
{
	FTDTravelStateTransition& Transition = Transitions.AddDefaulted_GetRef();
	Transition.OldState = OldState;
	Transition.NewState = NewState;
}

bool UTDTravelStateRecorder::HasTravelingWithoutPreloading() const
{
	bool bHasSeenPreloadingSinceIdle = InitialState != ETDSeamlessTravelState::Idle;
	for (const FTDTravelStateTransition& Transition : Transitions)
	{
		if (Transition.NewState == ETDSeamlessTravelState::Preloading)
		{
			bHasSeenPreloadingSinceIdle = true;
			continue;
		}
		if (Transition.NewState == ETDSeamlessTravelState::Traveling && !bHasSeenPreloadingSinceIdle)
		{
			return true;
		}
		if (Transition.NewState == ETDSeamlessTravelState::Idle)
		{
			bHasSeenPreloadingSinceIdle = false;
		}
	}
	return false;
}

int32 UTDTravelStateRecorder::CountTransitionsTo(ETDSeamlessTravelState State) const
{
	int32 Count = 0;
	for (const FTDTravelStateTransition& Transition : Transitions)
	{
		if (Transition.NewState == State)
		{
			++Count;
		}
	}
	return Count;
}

FString UTDTravelStateRecorder::DescribeTransitions() const
{
	FString Description = FString::Printf(TEXT("initial=%s"), TravelStateToText(InitialState));
	for (const FTDTravelStateTransition& Transition : Transitions)
	{
		Description += FString::Printf(TEXT(", %s->%s"), TravelStateToText(Transition.OldState), TravelStateToText(Transition.NewState));
	}
	return Description;
}
