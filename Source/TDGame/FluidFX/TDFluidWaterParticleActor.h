#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterParticleTypes.h"
#include "UObject/ObjectKey.h"
#include "TDFluidWaterParticleActor.generated.h"

class ATDFluidWaterActor;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraDataChannelAsset;
class UNiagaraSystem;
class UTDFluidLiveComponent;
struct FStreamableHandle;

UCLASS()
class TDGAME_API ATDFluidWaterParticleActor : public AActor
{
	GENERATED_BODY()

public:
	ATDFluidWaterParticleActor();

	UPROPERTY(EditInstanceOnly, Category = "WaterParticles")
	TObjectPtr<ATDFluidWaterActor> WaterActor;

	UPROPERTY(EditInstanceOnly, Category = "WaterParticles")
	TObjectPtr<AActor> SurfaceMaterialSourceActor;

	UPROPERTY(EditAnywhere, Category = "WaterParticles")
	TSoftObjectPtr<UNiagaraSystem> DebrisSystem;

	UPROPERTY(EditAnywhere, Category = "WaterParticles")
	TSoftObjectPtr<UNiagaraSystem> WhitewaterSystem;

	UPROPERTY(EditAnywhere, Category = "WaterParticles")
	TSoftObjectPtr<UNiagaraSystem> SplashSystem;

	UPROPERTY(EditAnywhere, Category = "WaterParticles")
	TSoftObjectPtr<UNiagaraDataChannelAsset> ContactDataChannel;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "WaterParticles")
	int32 GetActiveSplashCount() const;

	int32 GetEmittedSplashEventCount() const { return EmittedSplashEventCount; }
	int32 GetLastContactPointCount() const { return LastContactPointCount; }
	int32 GetLastContactStampCount() const { return LastContactStampCount; }
	UNiagaraComponent* GetDebrisComponent() const { return DebrisComponent; }
	UNiagaraComponent* GetWhitewaterComponent() const { return WhitewaterComponent; }
	bool SpawnSplashEvent(const FTDFluidWaterSplashEvent& SplashEvent);
	const FTDFluidWaterParticleSettings& GetSettings() const { return Settings; }
	void SetSurfaceParametersOverrideForTests(const FTDFluidWaterSurfaceParameters& Parameters) { SurfaceParametersOverride = Parameters; }
	void ClearSurfaceParametersOverrideForTests() { SurfaceParametersOverride.Reset(); }

private:
	UTDFluidLiveComponent* GetWaterLiveComponent() const;
	void ResolveSettingsIfStale();
	void HandlePresetTuningReloaded(int32 TableGeneration);
	void RequestMissingAssetLoads();
	void DeactivateParticleLayers();
	void DriveParticleLayer(TObjectPtr<UNiagaraComponent>& Component, UNiagaraSystem* System, int32& AppliedLookGeneration, double& ActivationSeconds, const FTDFluidWaterSurfaceParameters& Parameters);
	FTDFluidWaterSurfaceParameters ResolveWaterSurfaceParameters();
	UMaterialInterface* ResolveSurfaceMaterial();
	AActor* ResolveSurfaceSourceActor();
	bool TryGetSurfaceHeightAt(const FVector& WorldLocation, float& OutSurfaceHeightCm) const;
	float ComputeWaterCoverage() const;
	void EmitSplashEvents(float DeltaSeconds);
	void ApplyContactWhitewater();
	void PruneInactiveSplashes();

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> DebrisComponent;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> WhitewaterComponent;

	FTDFluidWaterParticleSettings Settings;
	TOptional<ETDFluidPresetKind> ResolvedPresetKind;
	TOptional<FTDFluidWaterSurfaceParameters> SurfaceParametersOverride;
	TMap<TPair<FObjectKey, int32>, FTDFluidWaterSplashPointState> SplashPointStates;
	TArray<TWeakObjectPtr<UNiagaraComponent>> ActiveSplashComponents;
	TWeakObjectPtr<AActor> CachedSurfaceSourceActor;
	TSharedPtr<FStreamableHandle> AssetLoadHandle;
	FDelegateHandle PresetTuningReloadedHandle;
	int32 DebrisLookGeneration = INDEX_NONE;
	int32 WhitewaterLookGeneration = INDEX_NONE;
	double DebrisActivationSeconds = 0.0;
	double WhitewaterActivationSeconds = 0.0;
	double LastCoverageRefreshSeconds = -1.0;
	double LastSurfaceSourceSearchSeconds = -1.0;
	float CachedWaterCoverage = 0.0f;
	int32 SplashSerial = 0;
	int32 EmittedSplashEventCount = 0;
	int32 LastSplashEventCount = 0;
	int32 LastContactPointCount = 0;
	int32 LastContactStampCount = 0;
	bool bHasRequestedAssetLoad = false;
};
