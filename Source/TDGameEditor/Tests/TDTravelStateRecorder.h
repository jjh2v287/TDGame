#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "World/Streaming/TDSeamlessTravelSubsystem.h"
#include "TDTravelStateRecorder.generated.h"

struct FTDTravelStateTransition
{
	ETDSeamlessTravelState OldState = ETDSeamlessTravelState::Idle;
	ETDSeamlessTravelState NewState = ETDSeamlessTravelState::Idle;
};

UCLASS()
class UTDTravelStateRecorder : public UObject
{
	GENERATED_BODY()

public:
	void BindTo(UTDSeamlessTravelSubsystem* Travel);
	void UnbindFrom(UTDSeamlessTravelSubsystem* Travel);

	bool HasTravelingWithoutPreloading() const;
	int32 CountTransitionsTo(ETDSeamlessTravelState State) const;
	FString DescribeTransitions() const;

	const TArray<FTDTravelStateTransition>& GetTransitions() const { return Transitions; }

private:
	UFUNCTION()
	void HandleTravelStateChanged(ETDSeamlessTravelState OldState, ETDSeamlessTravelState NewState);

	ETDSeamlessTravelState InitialState = ETDSeamlessTravelState::Idle;
	TArray<FTDTravelStateTransition> Transitions;
};
