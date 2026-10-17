#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/TDRealmDefinitions.h"
#include "TDRealmSubsystem.generated.h"

class APawn;
class APostProcessVolume;
class ATDGameCharacter;
class ATDRealmLiftDeck;
class ATDRealmShaftLiner;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UTDSeamlessTravelSubsystem;
struct FStreamableHandle;

UCLASS()
class TDGAME_API UTDRealmSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "TD|Realm")
	void ApplyAtmosphereForLocation(const FVector& LocationCm);

	UFUNCTION(BlueprintPure, Category = "TD|Realm")
	FName GetActiveRealmId() const { return ActiveRealmId; }

	UFUNCTION(BlueprintPure, Category = "TD|Realm")
	float GetRealmWeight(FName RealmId) const;

	UFUNCTION(BlueprintPure, Category = "TD|Realm")
	FTDRealmAtmosphere GetFieldAtmosphere() const { return FieldAtmosphere; }

	UFUNCTION(BlueprintPure, Category = "TD|Realm", meta = (WorldContext = "WorldContextObject"))
	static UTDRealmSubsystem* GetRealmSubsystem(const UObject* WorldContextObject);

	ATDRealmLiftDeck* FindRealmLiftDeck(FName GateId) const;
	int32 GetPassagePacingEngagedCount() const { return PassagePacingEngagedCount; }

	static float ComputeConnectorWeight(double AlongCm, double ApproachLengthCm, double DepartureLengthCm);
	static float ComputeLiftShaftWeight(double DepthCm, double TravelLengthCm);
	static float ComputePassagePaceTarget(double ApproachCm, bool bIsReady, bool bIsPreloadRejected, bool bIsMovingAway);
	static float StepPassagePaceScale(float CurrentScale, float TargetScale, float DeltaSeconds);

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

private:
	struct FTDPassageLocation
	{
		const UTDRealmDefinition* Realm = nullptr;
		const FTDRealmGate* Gate = nullptr;
		double AlongCm = 0.0;
		float Weight = 0.0f;
		bool bIsLift = false;
	};

	static FTDPassageLocation MakePassageLocation(const UTDRealmDefinition& Realm, const FTDRealmGate& Gate, double AlongCm, float Weight, bool bIsLift);
	static bool TryMeasureConnector(const UTDRealmDefinition& Realm, const FTDRealmGate& Gate, const FVector& LocationCm, FTDPassageLocation& OutPassage, double& OutDistanceSquared);

	void CollectConfiguredRealmDefinitions(TArray<TObjectPtr<UTDRealmDefinition>>& OutRealms) const;
	void RequestRealmDefinitions();
	void RequestRealmLiftAssets();
	void HandleRealmLiftAssetsLoaded();
	void SpawnRealmLifts();
	void SpawnShaftLiner(UWorld& World, const FTDRealmGate& Gate, ATDRealmLiftDeck& Lift);
	void CaptureFieldAtmosphere();
	FTDPassageLocation FindPassageLocation(const FVector& LocationCm) const;
	bool TryMeasureLiftGate(const UTDRealmDefinition& Realm, const FTDRealmGate& Gate, const FVector& LocationCm, FTDPassageLocation& OutPassage, double& OutDistanceSquared) const;
	FVector MeasurePawnVelocity(const FVector& PawnLocationCm, float DeltaTime);
	void UpdatePassageDirection(const APawn& Pawn, const FTDPassageLocation& Passage, const FVector& PawnVelocityCm);
	void UpdatePassagePreload(const FTDPassageLocation& Passage);
	void RecordOwnedPreload(FName GateId, bool bToEntry);
	void CancelOwnedPreload(UTDSeamlessTravelSubsystem& Travel);
	void ActivateGameplayDataLayers(const FVector& LocationCm, const FTDPassageLocation& Passage);
	void ActivateGameplayDataLayer(const UTDRealmDefinition& Realm, const TCHAR* Reason);
	void UpdatePassagePace(APawn& Pawn, const FTDPassageLocation& Passage, const FVector& PawnVelocityCm, float DeltaTime);
	void SetPassagePacingEngaged(bool bIsEngaged, bool bIsReady);
	void StopPassagePacing();
	void ApplyPassagePace();
	void RestorePassagePace();
	void UpdateAtmosphere(const FVector& LocationCm, const FTDPassageLocation& Passage);
	FTDRealmAtmosphere MakeBlendedAtmosphere(const UTDRealmDefinition& Realm, float Weight) const;
	void ApplyAtmosphere(const FTDRealmAtmosphere& Atmosphere);
	void ApplySun(UDirectionalLightComponent& Sun, const FTDRealmAtmosphere& Atmosphere);
	void ApplySunRotation(const FRotator& SunRotation);
	void ApplyFog(UExponentialHeightFogComponent& Fog, const FTDRealmAtmosphere& Atmosphere);
	bool RefreshRealmPostProcessVolumes();
	void ApplyPostProcessWeights();
	void RecoverFallenPawnIfNeeded(APawn& Pawn);
	bool RecoverPawnFromLiftPitIfNeeded(APawn& Pawn) const;
	void RecoverFallenPawn(APawn& Pawn, const UTDRealmDefinition& Realm);
	bool TeleportFallenPawn(APawn& Pawn, const FVector& RecoveryLocation, const FRotator& RecoveryRotation, const FLinearColor& FadeColor) const;
	bool IsTravelBlockingRecovery() const;
	FVector FindFallRecoveryLocation(const APawn& Pawn, const FTDRealmGate& Gate) const;
	FVector FindLiftExitRecoveryLocation(const APawn& Pawn, const FVector& ExitLocationCm) const;
	const UTDRealmDefinition* FindLoadedRealmContainingLocation(const FVector& LocationCm) const;
	APawn* FindLocalPlayerPawn() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTDRealmDefinition>> LoadedRealmDefinitions;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ATDRealmLiftDeck>> RealmLiftDecks;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ATDRealmShaftLiner>> RealmShaftLiners;

	TSharedPtr<FStreamableHandle> RealmDefinitionsHandle;
	TSharedPtr<FStreamableHandle> RealmLiftAssetsHandle;

	TWeakObjectPtr<ATDGameCharacter> PacedCharacter;
	float CurrentPassagePaceScale = 1.0f;
	float AppliedPassagePaceScale = 1.0f;
	FName PacingGateId;
	bool bIsPacingToEntry = true;
	bool bIsPacingArmed = false;
	bool bIsPassagePacingEngaged = false;
	double PacingAlongCm = 0.0;
	int32 PassagePacingEngagedCount = 0;

	FVector PreviousPawnLocationCm = FVector::ZeroVector;
	bool bHasPreviousPawnLocation = false;
	FName TrackedPassageGateId;
	bool bIsTrackedPassageToEntry = true;

	TSet<FName> ActivatedGameplayRealmIds;

	TMap<FName, TWeakObjectPtr<APostProcessVolume>> RealmPostProcessVolumes;
	TWeakObjectPtr<UDirectionalLightComponent> SunComponent;
	TWeakObjectPtr<USkyLightComponent> SkyLightComponent;
	TWeakObjectPtr<USkyAtmosphereComponent> SkyAtmosphereComponent;
	TWeakObjectPtr<UExponentialHeightFogComponent> HeightFogComponent;
	FTDRealmAtmosphere FieldAtmosphere;
	FName ActiveRealmId;
	FName CurrentRealmId;
	float CurrentRealmWeight = 0.0f;
	FName AppliedRealmId;
	int32 AppliedWeightStep = 0;
	FName AppliedSunRealmId;
	int32 AppliedSunStep = INDEX_NONE;
	FName OwnedPreloadGateId;
	bool bIsOwnedPreloadToEntry = true;
	double OwnedPreloadStartWorldSeconds = 0.0;
	FName FailedPreloadGateId;
	bool bIsFailedPreloadToEntry = true;
	double NextPostProcessScanSeconds = 0.0;
	bool bHasBegunPlay = false;
	bool bHasCapturedFieldAtmosphere = false;
	bool bHasWarnedStaticSun = false;
	bool bHasWarnedStaticFog = false;
};
