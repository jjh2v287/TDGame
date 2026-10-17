#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/TDRealmDefinitions.h"
#include "TDRealmLiftDeck.generated.h"

class AController;
class APawn;
class UBoxComponent;
class UMaterialInterface;
class UMoverStandaloneLiaisonComponent;
class UStaticMesh;
class UStaticMeshComponent;

enum class ETDRealmLiftState : uint8
{
	Resting,
	Boarding,
	Riding,
	Treadmill,
	Open,
	Calling
};

enum class ETDRealmLiftDirection : uint8
{
	Down,
	Up
};

struct TDGAME_API FTDRealmLiftMotion
{
	double TravelLengthCm = 4000.0;
	double CruiseSpeedCmPerSecond = 300.0;
	double AccelerationCmPerSecondSquared = 100.0;
	double BrakeDecelerationCmPerSecondSquared = 80.0;
	double HoldDepthCm = 1912.5;
	double RingModuleHeightCm = 450.0;
	double MaxTreadmillSeconds = 20.0;
	double BoardDwellSeconds = 1.2;
	double CallSpeedCmPerSecond = 600.0;

	ETDRealmLiftState State = ETDRealmLiftState::Resting;
	ETDRealmLiftDirection Direction = ETDRealmLiftDirection::Down;
	double PositionCm = 0.0;
	double SpeedCmPerSecond = 0.0;
	double RingSpeedCmPerSecond = 0.0;
	double RingPhaseCm = 0.0;
	double TreadmillSeconds = 0.0;
	bool bIsReadyLatched = false;
	bool bIsResumeArmed = false;
	bool bIsRingResuming = false;
	double ReadyChangeSeconds = 0.0;
	double BoardElapsedSeconds = 0.0;

	static FTDRealmLiftMotion MakeFromGate(const FTDRealmGate& Gate);

	double GetProgressCm() const;
	double GetTreadmillDecisionProgressCm() const;
	bool CanUseTreadmill() const;
	bool IsCarryingRider() const;
	bool ShouldShowAbyssScreen(double AbyssScreenDepthCm) const;
	bool IsAtFieldLanding() const;
	bool IsAtRealmLanding() const;

	void UpdateRider(bool bHasRider, double DeltaSeconds);
	void RequestCall(bool bToRealmLanding);
	void Step(double DeltaSeconds, bool bIsReady);
	void PlaceAtLanding(bool bAtRealmLanding);

private:
	double GetDirectionSign() const;
	double GetResumeDistanceCm() const;
	double GetBrakeDistanceCm() const;
	void StartRide();
	void ResetRideProgress();
	void UpdateReadyLatch(bool bIsReady, double DeltaSeconds);
	void StepTravel(double StepSeconds, bool bIsCalling);
	void FinishAtLanding(bool bIsCalling);
	void EnterTreadmill();
	void StepTreadmill(double StepSeconds);
	void StepTreadmillBrake(double StepSeconds);
	double ComputeResumeTriggerPhaseCm() const;
	void StepTreadmillResume(double StepSeconds);
	void FinishTreadmill(double RemainingSeconds);
	void SnapRingPhaseToModule();
};

UCLASS(NotPlaceable, Transient)
class TDGAME_API ATDRealmLiftDeck : public AActor
{
	GENERATED_BODY()

public:
	ATDRealmLiftDeck();

	void InitializeLift(const FTDRealmGate& Gate);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	static FTransform MakeShaftFrameTransform(const FTDRealmGate& Gate);
	static void GatherRequiredAssetPaths(const FTDRealmGate& Gate, TArray<FSoftObjectPath>& OutPaths);

	FName GetGateId() const { return GateId; }
	const FTDRealmLiftMotion& GetMotion() const { return Motion; }
	UStaticMeshComponent* GetDeckComponent() const { return DeckComponent; }
	FVector ComputeDeckLocation(double PositionCm) const;
	FVector GetDeckTopLocation() const;
	bool IsCarryingPawn(const APawn& Pawn) const;
	bool IsCarryingLocation(const FVector& LocationCm) const;
	bool IsPawnBasedOnDeck(const APawn& Pawn) const;
	bool IsAnyDoorOpen() const;

private:
	void ConfigureEdges(const FTDRealmGate& Gate, UStaticMesh* RailMesh, UMaterialInterface* Material);
	void ConfigureAbyssScreen(const FTDRealmGate& Gate);
	APawn* FindLocalPlayerPawn() const;
	bool IsPawnAboard(const APawn& Pawn) const;
	bool IsWithinRideVolume(const FVector& LocationCm) const;
	bool IsInsideDeckFootprint(const FVector& LocalLocationCm, double MarginCm) const;
	bool IsPawnInCallZone(const APawn& Pawn, bool bIsRealmLanding) const;
	void RequestCallForPawn(const APawn& Pawn);
	bool IsDestinationReady() const;
	void LogTreadmillChange(ETDRealmLiftState PreviousState, bool bWasResumeArmed) const;
	void UpdateRiderLock(APawn* Rider);
	void LockRider(APawn& Rider);
	void ReleaseRider();
	void KeepRiderOnDeck(APawn* Rider);
	void UpdateDoors();
	void UpdateAbyssScreen();
	void MoveDeckToMotion();

	UPROPERTY(VisibleAnywhere, Category = "Lift")
	TObjectPtr<USceneComponent> DeckRoot;

	UPROPERTY(VisibleAnywhere, Category = "Lift")
	TObjectPtr<UStaticMeshComponent> DeckComponent;

	UPROPERTY(VisibleAnywhere, Category = "Lift")
	TObjectPtr<UStaticMeshComponent> AbyssScreenComponent;

	UPROPERTY(VisibleAnywhere, Category = "Lift")
	TArray<TObjectPtr<UStaticMeshComponent>> EdgeRails;

	UPROPERTY(VisibleAnywhere, Category = "Lift")
	TArray<TObjectPtr<UBoxComponent>> EdgeBlockers;

	FName GateId;
	FTDRealmLiftMotion Motion;
	FVector FieldLandingLocationCm = FVector::ZeroVector;
	FVector DeckHalfExtentCm = FVector(300.0, 300.0, 15.0);
	double AbyssScreenDepthCm = 600.0;
	uint8 FieldExitEdgeMask = 0;
	uint8 RealmExitEdgeMask = 0;
	uint8 BlockedEdgeMask = 0;
	bool bHasLockedRider = false;
	int32 RiderSnapCount = 0;
	TWeakObjectPtr<APawn> LockedPawn;
	TWeakObjectPtr<AController> LockedController;
	TWeakObjectPtr<UMoverStandaloneLiaisonComponent> LockedLiaison;
};
