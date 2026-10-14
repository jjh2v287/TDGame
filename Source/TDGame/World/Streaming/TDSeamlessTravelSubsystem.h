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

class APawn;
class APlayerCameraManager;

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

	UFUNCTION(BlueprintCallable, Category = "TD|Travel")
	bool CancelPreload(FName DungeonId, bool bToEntry);

	UFUNCTION(BlueprintPure, Category = "TD|Travel")
	bool IsReadyFor(FName DungeonId, bool bToEntry) const;

	UFUNCTION(BlueprintCallable, Category = "TD|Travel")
	bool CommitSeamCrossing(FName DungeonId, bool bToEntry, const FVector& HoldLocation);

	UFUNCTION(BlueprintPure, Category = "TD|Travel", meta = (WorldContext = "WorldContextObject"))
	static UTDSeamlessTravelSubsystem* GetSeamlessTravelSubsystem(const UObject* WorldContextObject);

	static bool TeleportPawnToDestination(APawn& Pawn, const FVector& TargetLocation, const FRotator& TargetRotation);

	UFUNCTION(BlueprintPure, Category = "TD|Travel")
	ETDSeamlessTravelState GetTravelState() const { return TravelState; }

	bool IsUnrequestedPreload() const;

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
	bool ResolveDestination(FName DungeonId, bool bToEntry, FTransform& OutDestination, FLinearColor& OutFadeColor) const;
	bool IsSameTarget(FName DungeonId, bool bToEntry) const;
	bool StartPreload(FName DungeonId, bool bToEntry);
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
	APlayerCameraManager* FindLocalCameraManager() const;
	void BeginTravelFadeOut();
	void BeginTravelFadeIn();
	double GetWorldTimeSeconds() const;
	bool IsDepartureHoldActive() const;
	void BeginDepartureHold(const FVector& DepartureLocation);
	double GetElapsedSeconds() const;
	float GetTimeoutSeconds() const;

	ETDSeamlessTravelState TravelState = ETDSeamlessTravelState::Idle;
	FName ActiveDungeonId;
	FName LastTraveledDungeonId;
	bool bIsTargetEntry = true;
	bool bIsTravelRequested = false;
	bool bIsProviderRegistered = false;
	bool bIsTravelFadeActive = false;
	FTransform Destination;
	FLinearColor TravelFadeColor = FLinearColor::Black;
	double TravelFadeOutEndSeconds = 0.0;
	FVector DepartureHoldLocation = FVector::ZeroVector;
	double DepartureHoldEndSeconds = 0.0;
	double RequestStartSeconds = 0.0;
	double NextTimeoutWarningSeconds = 0.0;
	FDelegateHandle StreamingStateUpdatedHandle;
};
