#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "TDSeamlessTravelSubsystem.generated.h"

UENUM(BlueprintType)
enum class ETDSeamlessTravelState : uint8
{
	Idle,
	Preloading,
	ReadyToTravel,
	Traveling,
	Settling
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTDTravelStateChanged, ETDSeamlessTravelState, OldState, ETDSeamlessTravelState, NewState);

UCLASS()
class TDGAME_API UTDSeamlessTravelSubsystem : public UTickableWorldSubsystem, public IWorldPartitionStreamingSourceProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "TD|Travel")
	FTDTravelStateChanged OnTravelStateChanged;

	UFUNCTION(BlueprintCallable, Category = "TD|Travel")
	bool BeginPreload(FName DungeonId, bool bToEntry);

	UFUNCTION(BlueprintCallable, Category = "TD|Travel")
	bool RequestTravel(FName DungeonId, bool bToEntry);

	UFUNCTION(BlueprintCallable, Category = "TD|Travel")
	void CancelTravel();

	UFUNCTION(BlueprintPure, Category = "TD|Travel")
	ETDSeamlessTravelState GetTravelState() const { return TravelState; }

	UFUNCTION(BlueprintPure, Category = "TD|Travel")
	FName GetActiveDungeonId() const { return ActiveDungeonId; }

	UFUNCTION(BlueprintPure, Category = "TD|Travel")
	FName GetLastTraveledDungeonId() const { return LastTraveledDungeonId; }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	virtual bool GetStreamingSources(TArray<FWorldPartitionStreamingSource>& StreamingSources) const override;

private:
	bool ResolveDestination(FName DungeonId, bool bToEntry, FTransform& OutDestination) const;
	bool IsSameTarget(FName DungeonId, bool bToEntry) const;
	bool IsDestinationReady() const;
	bool IsDestinationOnNavigation() const;
	void RegisterStreamingProvider();
	void UnregisterStreamingProvider();
	void HandleStreamingStateUpdated();
	void SetTravelState(ETDSeamlessTravelState NewState);
	void TickPreloading();
	void TickReadyToTravel();
	bool PerformTravel();
	APawn* FindLocalPlayerPawn() const;
	double GetElapsedSeconds() const;
	float GetTimeoutSeconds() const;

	ETDSeamlessTravelState TravelState = ETDSeamlessTravelState::Idle;
	FName ActiveDungeonId;
	FName LastTraveledDungeonId;
	bool bIsTargetEntry = true;
	bool bIsTravelRequested = false;
	bool bIsProviderRegistered = false;
	FTransform Destination;
	double RequestStartSeconds = 0.0;
	double NextTimeoutWarningSeconds = 0.0;
	FDelegateHandle StreamingStateUpdatedHandle;
};
