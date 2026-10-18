#pragma once

#include "CoreMinimal.h"
#include "FluidFX/TDFluidTypes.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectKey.h"
#include "TDFluidLiveActor.generated.h"

class APlayerController;
class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;
class UTDCombatComponent;
class UTDFluidLiveComponent;
class UTDFluidPresetAsset;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveVolume")
	TObjectPtr<AActor> VolumeLightActor;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveTerrain", meta = (ClampMin = "1"))
	int32 MaxTerrainTracesPerTick = 512;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void InitializeFromPreset(UTDFluidPresetAsset* InPreset, float InActiveDurationSeconds = 0.0f);

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

private:
	struct FTDFluidCombatBinding
	{
		TWeakObjectPtr<UTDCombatComponent> CombatComponent;
		FDelegateHandle DamageHandle;
		FDelegateHandle DeathHandle;
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
	void ResetInteractionHistory();
	void ClearInteractionState();
	void InvalidateTerrainQueueIfSolverGridChanged();
	void ShiftTerrainQueueWithWindow(const FVector2D& CenterBeforeStep);
	void SampleQueuedTerrainWithinBudget();
	FVector2D ResolveDesiredSimulationCenter() const;
	void UpdatePlaneBasis();
	void PlaceChildrenOnSolverPlane();
	bool ResolveMouseBrushWorldLocation(const APlayerController& Controller, FVector& OutLocation) const;

	TArray<TWeakObjectPtr<AActor>> RegisteredInteractingActors;
	TMap<FObjectKey, FVector> PreviousActorLocations;
	TMap<TPair<FObjectKey, FName>, FVector> PreviousBoneLocations;
	TMap<FObjectKey, FVector> CurrentActorLocations;
	TMap<TPair<FObjectKey, FName>, FVector> CurrentBoneLocations;
	TMap<FObjectKey, FTDFluidCombatBinding> CombatBindings;
	TArray<FIntPoint> PendingTerrainCells;
	FVector2D TerrainQueuedExtentCm = FVector2D::ZeroVector;
	int32 TerrainQueuedResolution = 0;
	int32 TerrainQueuedGeneration = 0;
	FVector2D CenterBeforeSolverStep = FVector2D::ZeroVector;
	FVector PreviousMouseBrushLocation = FVector::ZeroVector;

	bool bHasPreviousMouseBrushLocation = false;
	bool bIsPooledInactive = false;
	bool bHasFiniteLifetime = false;
	bool bHasSteppedWhileAwake = false;
	bool bIsTerrainQueueCurrent = false;
	bool bHasPreparedSolverStep = false;
	float RemainingLifetimeSeconds = 0.0f;
	float LastStepDeltaSeconds = 0.0f;
};

