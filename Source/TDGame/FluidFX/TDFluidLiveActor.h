#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTerrainCollisionMask.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidVolumeDriverTypes.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectKey.h"
#include "TDFluidLiveActor.generated.h"

struct FCollisionQueryParams;
struct FTDFluidPresetReapplyReport;
struct FTraceDatum;
struct FTraceHandle;

class APlayerController;
class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;
class UTDCombatComponent;
class UTDFluidHeightCacheAsset;
class UTDFluidLiveComponent;
class UTDFluidPresetAsset;
class UTDFluidVolumeDriverComponent;
class UTexture2D;
struct FStreamableHandle;
class UWorld;

struct FTDFluidTrackGroundPlane
{
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	bool bIsValid = false;
};

UCLASS()
class TDGAME_API ATDFluidLiveActor : public AActor
{
	GENERATED_BODY()

public:
	ATDFluidLiveActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LiveActor")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LiveActivation")
	TObjectPtr<UBoxComponent> ActivationVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LiveInteraction")
	TObjectPtr<UBoxComponent> InteractionVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LiveActor")
	TObjectPtr<UStaticMeshComponent> InternalSurfaceMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LiveActor")
	TObjectPtr<UStaticMeshComponent> VolumeMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LiveActor")
	TObjectPtr<UTDFluidLiveComponent> LiveComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveInteraction")
	TObjectPtr<ATDFluidLiveActor> WetnessSourceActor;

	static int32 ComputeStrokeStampCount(double SegmentLengthCm, float RadiusCm, float SpacingRatio);
	static bool IsTrackContactStampable(const FVector& Velocity, float MinSpeedCmPerSec, float MaxSpeedCmPerSec, const FTDFluidInputBrushSettings& Settings);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveVolume")
	TObjectPtr<AActor> VolumeLightActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveVolume")
	bool bCastPawnCapsuleShadows = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveVolume")
	TArray<FTDFluidShadowCapsuleBonePair> ShadowCapsuleBones;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveActivation")
	bool bShouldUseActivationVolume = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveActivation", meta = (ClampMin = "100.0"))
	float ActivationDistanceCm = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveInteraction")
	bool bShouldFollowPlayerPawn = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveInteraction")
	TWeakObjectPtr<AActor> FollowTargetActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveInteraction")
	TArray<FName> TrackedBoneNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveInteraction", meta = (ClampMin = "5.0"))
	float BoneBrushRadiusCm = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveInteraction", meta = (ClampMin = "5.0"))
	float ActorPivotBrushRadiusCm = 65.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveInteraction", meta = (ClampMin = "0.1"))
	float MinInteractionSpeedCmPerSec = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveInteraction")
	ETDFluidBrushChannel InteractionBrushChannel = ETDFluidBrushChannel::ChannelR_Mud;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveInteraction")
	bool bAcceptMouseBrush = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveInteraction", meta = (ClampMin = "5.0"))
	float MouseBrushRadiusCm = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveTerrain")
	bool bSampleTerrainElevation = false;

	bool ShouldSampleTerrainElevation() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveTerrain", meta = (ClampMin = "1"))
	int32 MaxTerrainTracesPerTick = 512;

	UPROPERTY(EditAnywhere, Category = "FluidFX")
	bool bUseAsyncTerrainTraces = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveTerrain")
	TSoftObjectPtr<UTDFluidHeightCacheAsset> TerrainHeightCache;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "LiveVolume")
	TObjectPtr<UTDFluidVolumeDriverComponent> VolumeDriver;

	static float ApplyTerrainHeightOps(float ElevationCm, const FTDFluidTerrainHeightSettings& Settings);
	void SetTerrainHeightSettings(const FTDFluidTerrainHeightSettings& InSettings);
	const FTDFluidTerrainHeightSettings& GetTerrainHeightSettings() const { return TerrainHeightSettings; }
	bool TrySampleTerrainHeightCm(const FVector2D& WorldXY, double& OutHeightCm) const;
	UTexture2D* GetTerrainHeightCacheTexture() const { return TerrainHeightCacheTexture; }
	bool GetTerrainHeightCacheWindow(FVector4f& OutWindow) const;
	int32 GetTerrainSamplingCompletionCount() const { return TerrainSamplingCompletionCount; }
	int32 GetTerrainTraceRequestCount() const { return TerrainTraceRequestCount; }
	int32 GetTerrainCacheSampleCount() const { return TerrainCacheSampleCount; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void InitializeFromPreset(UTDFluidPresetAsset* InPreset, float InActiveDurationSeconds = 0.0f);
	virtual void ReapplyPresetTuning(FTDFluidPresetReapplyReport& InOutReport);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void RegisterInteractingActor(AActor* InteractingActor);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void UnregisterInteractingActor(AActor* InteractingActor);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void EvaluateActivationProximity(const FVector& ObserverWorldLocation);

	void QueueMouseBrushAt(const FVector& CursorWorldLocation, float DeltaTime);

	void StepLiveActor(float DeltaTime);
	bool BeginLiveActorStep(float DeltaTime);
	void RunLiveActorSolverStep(float DeltaTime);
	void FinishLiveActorStep();

	bool IsPooledInactive() const { return bIsPooledInactive; }
	void SetPooledInactive(bool bInactive);
	bool HasFiniteLifetimeExpired() const { return bHasFiniteLifetime && RemainingLifetimeSeconds <= 0.0f; }
	int32 GetBoundCombatComponentCount() const { return CombatBindings.Num(); }

protected:
	virtual FVector ResolvePlaneAnchorWorldLocation() const;
	virtual bool ShouldHarvestInteractions() const { return true; }
	virtual FVector2D GetDynamicCenterOffsetPlane() const { return FVector2D::ZeroVector; }
	virtual void PrepareActorSpecificInputs(float DeltaTime) {}
	virtual void FinishActorSpecificOutputs(float DeltaTime) {}
	virtual float ResolveTerrainMissElevationCm(int32 CellX, int32 CellY) const { return static_cast<float>(GetActorLocation().Z); }
	virtual void AddTerrainTraceIgnoredActors(FCollisionQueryParams& QueryParams) const {}
	virtual void OnTerrainSamplingCompleted() {}
	bool IsTerrainSamplingComplete() const { return bIsTerrainQueueCurrent && PendingTerrainCells.IsEmpty() && TerrainTraceSlots.IsEmpty() && !bShouldCompleteTerrainSampling; }

private:
	struct FTDFluidCombatBinding
	{
		TWeakObjectPtr<UTDCombatComponent> CombatComponent;
		FDelegateHandle DamageHandle;
		FDelegateHandle DeathHandle;
	};

	struct FTDFluidTerrainTraceSlot
	{
		FIntPoint Cell = FIntPoint::ZeroValue;
		float ImpactZ = 0.0f;
		bool bIsDelivered = false;
		bool bDidHit = false;
		bool bIsSettled = false;
	};

	void SyncVolumeExtentsWithSimulation();
	void CollectOverlappingAndRegisteredInputs(float DeltaTime);
	void HarvestActorInteractionStamps(AActor* TargetActor, float DeltaTime);
	void BindCombatComponentIfPresent(AActor* TargetActor);
	void PruneInvalidCombatBindings();
	void ReleaseCombatBindings();
	void PollMouseBrush(float DeltaTime);
	void ResetMouseBrushStroke();
	void ApplyVolumeLightToMaterial();
	void GatherShadowCapsules(TArray<FTDFluidShadowCapsule>& OutCapsules) const;
	void PushVolumeTransmittanceSources();
	void ResetInteractionHistory();
	void ClearInteractionState();
	void InvalidateTerrainQueueIfSolverGridChanged();
	void ShiftTerrainQueueWithWindow(const FVector2D& CenterBeforeStep);
	void SampleQueuedTerrainWithinBudget();
	FCollisionQueryParams BuildTerrainTraceQueryParams(const UWorld& World) const;
	void RequestQueuedTerrainTraces(UWorld& World);
	void OnTerrainTraceFinished(const FTraceHandle& TraceHandle, FTraceDatum& TraceDatum);
	void ApplyDeliveredTerrainTraceResults();
	void DiscardTerrainTraceSlots();
	void CompleteTerrainSamplingIfSettled();
	void SyncVolumeDriver();
	void SyncTerrainHeightCache();
	void HandleTerrainHeightCacheLoaded();
	void PushTerrainHeightCacheToDriver();
	bool IsTerrainHeightCacheSourceActive() const;
	bool IsTerrainHeightCacheLoading() const;
	int32 FillPendingTerrainCellsFromCache();
	void RecordTerrainCellElevation(int32 CellX, int32 CellY, float ElevationCm);
	void RebuildTerrainCollisionObstacles();
	FVector2D ResolveDesiredSimulationCenter() const;
	FVector2D ResolveNearRippleCenter() const;
	void UpdatePlaneBasis();
	void PlaceChildrenOnSolverPlane();
	bool ResolveMouseBrushWorldLocation(const APlayerController& Controller, FVector& OutLocation) const;

	TArray<TWeakObjectPtr<AActor>> RegisteredInteractingActors;
	TMap<FObjectKey, FVector> PreviousActorLocations;
	TMap<TPair<FObjectKey, FName>, FVector> PreviousBoneLocations;
	TMap<FObjectKey, FVector> CurrentActorLocations;
	TMap<TPair<FObjectKey, FName>, FVector> CurrentBoneLocations;
	TMap<TPair<FObjectKey, int32>, FVector> PreviousTrackPointLocations;
	TMap<TPair<FObjectKey, int32>, FVector> CurrentTrackPointLocations;
	int32 GroundTracesThisTick = 0;

	FTDFluidTrackGroundPlane TraceTrackGroundPlane(const AActor& TargetActor);
	void QueueTrackStroke(FTDFluidBrushStamp Stamp, const FVector& PreviousLocation, const FTDFluidInputBrushSettings& Settings, const FTDFluidTrackGroundPlane& GroundPlane);
	void HarvestTrackSourceStamps(AActor& TargetActor, float DeltaTime, const FTDFluidInputBrushSettings& Settings, const FTDFluidTrackGroundPlane& GroundPlane);
	TMap<FObjectKey, FTDFluidCombatBinding> CombatBindings;
	TArray<FIntPoint> PendingTerrainCells;
	TArray<FTDFluidTerrainTraceSlot> TerrainTraceSlots;
	FVector2D TerrainQueuedExtentCm = FVector2D::ZeroVector;
	int32 TerrainQueuedResolution = 0;
	int32 TerrainQueuedGeneration = 0;
	int32 AwaitedTerrainTraceCount = 0;
	uint32 TerrainTraceFirstSerial = 0;
	FVector2D CenterBeforeSolverStep = FVector2D::ZeroVector;
	FTDFluidTerrainHeightSettings TerrainHeightSettings;
	FTDFluidTerrainCollisionMask TerrainCollisionMask;

	UPROPERTY(Transient)
	TObjectPtr<UTDFluidHeightCacheAsset> LoadedTerrainHeightCache;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TerrainHeightCacheTexture;

	TSharedPtr<FStreamableHandle> TerrainHeightCacheLoadHandle;
	int32 TerrainCacheMissPrefixCount = 0;
	int32 TerrainSamplingCompletionCount = 0;
	int32 TerrainTraceRequestCount = 0;
	int32 TerrainCacheSampleCount = 0;
	bool bHasWarnedTerrainHeightCacheUnavailable = false;
	FVector PreviousMouseBrushLocation = FVector::ZeroVector;

	bool bHasPreviousMouseBrushLocation = false;
	bool bIsPooledInactive = false;
	bool bHasFiniteLifetime = false;
	bool bHasSteppedWhileAwake = false;
	bool bIsTerrainQueueCurrent = false;
	bool bShouldCompleteTerrainSampling = false;
	bool bHasPreparedSolverStep = false;
	float RemainingLifetimeSeconds = 0.0f;
	float LastStepDeltaSeconds = 0.0f;
};

