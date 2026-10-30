#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidWaterLodGridComponent.h"
#include "TDFluidWaterActor.generated.h"

class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UTDFluidDenseWaterCacheAsset;
struct FStreamableHandle;

UCLASS()
class TDGAME_API ATDFluidWaterActor : public ATDFluidLiveActor
{
	GENERATED_BODY()

public:
	ATDFluidWaterActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	FName WaterSurfaceActorTag = FName(TEXT("TDFluidWaterSurface"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	FName WaterRegionActorTag = FName(TEXT("TDFluidWaterRegion"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	FName WaterObstacleActorTag = FName(TEXT("TDFluidObstacle"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	FName TerrainTraceIgnoreTag = FName(TEXT("TDFluidTraceIgnore"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	bool bSleepWhenNoWaterInWindow = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0"))
	float SleepDelaySeconds = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0"))
	float ViewLookAheadCm = 190.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0"))
	float DeepWaterFallbackDepthCm = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.0"))
	float WaterLayerFadeInSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water|Wetness")
	bool bEnableWetDecal = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water|Wetness")
	TSoftObjectPtr<UMaterialInterface> WetDecalMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water|Wetness", meta = (ClampMin = "0.0"))
	float WetDecalVerticalMarginCm = 30.0f;

	UPROPERTY(VisibleAnywhere, Category = "Water|LodGrid")
	TObjectPtr<UTDFluidWaterLodGridComponent> LodGridComponent;

	UPROPERTY(EditAnywhere, Category = "Water|LodGrid")
	TSoftObjectPtr<UStaticMesh> LodGridPatchMesh;

	UPROPERTY(EditInstanceOnly, Category = "Water|TileMap")
	TObjectPtr<ATDFluidLiveActor> TileMapSourceActor;

	UPROPERTY(EditAnywhere, Category = "Water|Dense")
	TSoftObjectPtr<UTDFluidDenseWaterCacheAsset> DenseWaterCache;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void ReapplyPresetTuning(FTDFluidPresetReapplyReport& InOutReport) override;

	UFUNCTION(BlueprintPure, Category = "Water|LodGrid")
	FTDFluidWaterLodGridStats GetLodGridStats() const { return LodGridStats; }

	UFUNCTION(BlueprintPure, Category = "Water|Wetness")
	bool IsWetDecalVisible() const;

	UFUNCTION(BlueprintPure, Category = "Water|Wetness")
	UMaterialInstanceDynamic* GetWetDecalMaterialInstance() const;

	UFUNCTION(BlueprintPure, Category = "Water")
	int32 GetDrivenSurfaceCount() const { return DrivenSurfaceCount; }

	UFUNCTION(BlueprintPure, Category = "Water")
	int32 GetSharedSurfaceMaterialCount() const { return SharedSurfaceMaterials.Num(); }

	UFUNCTION(BlueprintPure, Category = "Water")
	float GetWaterLayerDisplayAlpha() const { return WaterLayerDisplayAlpha; }

	UFUNCTION(BlueprintCallable, Category = "Water|Dense")
	bool BakeDenseWaterCache(UTDFluidDenseWaterCacheAsset* TargetCache, float SimulatedSeconds);

protected:
	virtual FVector ResolvePlaneAnchorWorldLocation() const override;
	virtual void PrepareActorSpecificInputs(float DeltaTime) override;
	virtual void FinishActorSpecificOutputs(float DeltaTime) override;
	virtual float ResolveTerrainMissElevationCm(int32 CellX, int32 CellY) const override;
	virtual void AddTerrainTraceIgnoredActors(FCollisionQueryParams& QueryParams) const override;
	virtual void OnTerrainSamplingCompleted() override;

private:
	void TickWaterLayer(float DeltaSeconds);
	void RequestWetDecalMaterial();
	void ApplyLoadedWetDecalMaterial();
	void UpdateWetDecalPlacement();
	void HideWetDecal();
	void PlaceWetDecalIfChanged(const FVector& WorldLocation, const FVector& DecalSize);
	const AActor* ResolveFollowedActor() const;
	FVector ComputeLookAheadOffset() const;
	FBox2D ComputeWindowBoundsXY(double HalfExtentScale) const;
	void RefreshWaterRegistryIfChanged();
	void RebuildWaterRegistryCaches();
	void RebuildSharedSurfaceMaterials(const TArray<AActor*>& SurfaceActors);
	UMaterialInterface* ResolveSharedMaterialParent(UMaterialInterface* Material) const;
	void RefreshSolverWaterInputs();
	bool DoesWindowContainWater() const;
	void UpdateWaterPresence(float DeltaSeconds);
	void SleepForMissingWater();
	void WakeForPresentWater();
	void ResetWaterLayerFade();
	void RequestLodGridPatchMesh();
	void UpdateLodGrid();
	bool CanUseLodGrid() const;
	bool PrepareLodGridPatchMesh();
	bool CollectLodGridCoverage(FBox2D& OutCoverageCm, double& OutSurfaceHeightCm) const;
	UMaterialInstanceDynamic* ResolveLodGridMaterial(const FBox2D& CoverageCm, TArray<AActor*>& OutCoveredSurfaceActors) const;
	FVector ResolveLodGridObserverWorld() const;
	bool ShouldReselectLodGrid(const FVector& ObserverWorld) const;
	void PushLodGridMaterialParameters(UMaterialInstanceDynamic& LodMaterial, const FVector& ObserverWorld, double EffectiveBaseRangeCm) const;
	void SetTaggedSurfacesHiddenInGame(const TArray<AActor*>& SurfaceActors, bool bShouldHide);
	void DisableLodGrid();
	void RequestDenseWaterCacheLoad();
	void ApplyLoadedDenseWaterCache();

	UPROPERTY(Transient)
	TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInstanceDynamic>> SharedSurfaceMaterials;

	UPROPERTY(VisibleAnywhere, Category = "Water|Wetness")
	TObjectPtr<UDecalComponent> WetDecal;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WetDecalMID;

	TSharedPtr<FStreamableHandle> WetDecalLoadHandle;
	TSharedPtr<FStreamableHandle> LodGridMeshLoadHandle;
	TSharedPtr<FStreamableHandle> DenseWaterCacheLoadHandle;
	TArray<FTDFluidSdfObstacle> CachedWaterObstacles;
	TArray<TWeakObjectPtr<AActor>> LodHiddenSurfaceActors;
	FTDFluidWaterLodGridSettings LodGridSettings;
	FTDFluidWaterLodGridStats LodGridStats;
	FVector LastLodObserverWorld = FVector::ZeroVector;
	int32 LodAppliedRegistryVersion = INDEX_NONE;
	bool bIsLodGridActive = false;
	bool bIsLodSelectionDirty = true;
	bool bHasLodGridSettings = false;
	bool bHasWarnedLodGridMesh = false;
	bool bIsWaitingForDenseWaterCache = false;
	bool bHasWarnedDenseWaterCache = false;

	TArray<TWeakObjectPtr<AActor>> TraceIgnoredActors;
	TArray<FTDFluidWaterRegion> CachedWaterRegions;
	FVector2D GuideCollectionCenter = FVector2D::ZeroVector;
	int32 AppliedRegistryVersion = INDEX_NONE;
	int32 TrackedSolverGeneration = INDEX_NONE;
	int32 DrivenSurfaceCount = 0;
	int32 AppliedObstacleCount = 0;
	float SecondsWithoutWater = 0.0f;
	float PresenceCheckAccumulatorSeconds = 0.0f;
	float FadeElapsedSeconds = 0.0f;
	float WaterLayerDisplayAlpha = 0.0f;
	bool bIsDormantDuplicate = false;
	bool bIsSleepingForMissingWater = false;
	bool bHasWaterInWindow = false;
	bool bIsPresenceCheckPending = true;
	bool bAreWaterInputsDirty = true;
	bool bHasCollectedGuides = false;
	bool bTerrainSamplingFinished = false;
};
