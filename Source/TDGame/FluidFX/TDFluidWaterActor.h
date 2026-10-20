#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidWaterActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

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

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Water")
	int32 GetDrivenSurfaceCount() const { return DrivenSurfaceCount; }

	UFUNCTION(BlueprintPure, Category = "Water")
	int32 GetSharedSurfaceMaterialCount() const { return SharedSurfaceMaterials.Num(); }

	UFUNCTION(BlueprintPure, Category = "Water")
	float GetWaterLayerDisplayAlpha() const { return WaterLayerDisplayAlpha; }

protected:
	virtual FVector ResolvePlaneAnchorWorldLocation() const override;
	virtual void PrepareActorSpecificInputs(float DeltaTime) override;
	virtual void FinishActorSpecificOutputs(float DeltaTime) override;
	virtual float ResolveTerrainMissElevationCm(int32 CellX, int32 CellY) const override;
	virtual void AddTerrainTraceIgnoredActors(FCollisionQueryParams& QueryParams) const override;
	virtual void OnTerrainSamplingCompleted() override;

private:
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

	UPROPERTY(Transient)
	TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInstanceDynamic>> SharedSurfaceMaterials;

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
