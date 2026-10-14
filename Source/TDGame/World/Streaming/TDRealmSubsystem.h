#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/TDRealmDefinitions.h"
#include "TDRealmSubsystem.generated.h"

class APawn;
class APostProcessVolume;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UTDSeamlessTravelSubsystem;

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
		bool bIsRealmSide = false;
		double AlongCm = 0.0;
	};

	void LoadConfiguredRealmDefinitions();
	void CaptureFieldAtmosphere();
	FTDPassageLocation FindPassageLocation(const FVector& LocationCm) const;
	void UpdatePassagePreload(const FTDPassageLocation& Passage);
	void CancelOwnedPreload(UTDSeamlessTravelSubsystem& Travel);
	bool TryCrossSeam(APawn& Pawn, const FTDPassageLocation& Passage);
	bool ShiftPawnAcrossSeam(APawn& Pawn, const FVector& OffsetCm) const;
	void UpdateAtmosphere(const FVector& LocationCm, const FTDPassageLocation& Passage);
	FTDRealmAtmosphere MakeBlendedAtmosphere(const UTDRealmDefinition& Realm, const FTDPassageLocation& Passage, float Weight, float Mist) const;
	void ApplyAtmosphere(const FTDRealmAtmosphere& Atmosphere);
	void ApplySun(UDirectionalLightComponent& Sun, const FTDRealmAtmosphere& Atmosphere);
	void ApplySunRotation(const FRotator& SunRotation);
	void ApplyFog(UExponentialHeightFogComponent& Fog, const FTDRealmAtmosphere& Atmosphere);
	bool RefreshRealmPostProcessVolumes();
	void ApplyPostProcessWeights();
	void RecoverFallenPawnIfNeeded(APawn& Pawn) const;
	void RecoverFallenPawn(APawn& Pawn, const UTDRealmDefinition& Realm) const;
	FVector FindFallRecoveryLocation(const APawn& Pawn, const FTDRealmGate& Gate) const;
	const UTDRealmDefinition* FindLoadedRealmContainingLocation(const FVector& LocationCm) const;
	APawn* FindLocalPlayerPawn() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTDRealmDefinition>> LoadedRealmDefinitions;

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
	FName AppliedGateId;
	bool bIsAppliedRealmSide = false;
	int32 AppliedWeightStep = 0;
	int32 AppliedMistStep = 0;
	FName AppliedSunRealmId;
	int32 AppliedSunStep = INDEX_NONE;
	FName OwnedPreloadGateId;
	bool bIsOwnedPreloadToEntry = true;
	FName FailedPreloadGateId;
	bool bIsFailedPreloadToEntry = true;
	double NextPostProcessScanSeconds = 0.0;
	bool bHasBegunPlay = false;
	bool bHasWarnedSeamNotReady = false;
	bool bHasCapturedFieldAtmosphere = false;
	bool bHasWarnedStaticSun = false;
	bool bHasWarnedStaticFog = false;
};
