#pragma once

#include "CoreMinimal.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "FluidFX/TDFluidTypes.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "TDFluidLiveSubsystem.generated.h"

class ATDFluidLiveActor;
class ATDFluidRiverActor;
class FTDFluidGpuSolverRenderState;
struct FTDFluidGpuStateLayout;
struct FTDFluidStateChannelBytes;
class FTDFluidMassInputHub;
class UTDFluidObstacleComponent;
class UTDFluidFieldInputComponent;
struct FTDFluidSdfObstacle;
class ULevel;
class UMaterialInstanceDynamic;
class UTDFluidPresetAsset;
struct FTDActorDeathMessage;
struct FTDDamageAppliedMessage;

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidPresetReapplyReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 TableGeneration = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	FString TuningSourcePath;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	FString LookSourcePath;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 ReappliedActorCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 StateResetActorCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 ChangedFieldCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 LiveMaterialCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "FluidFX")
	int32 BakeOnlyChangedCount = 0;

	template <typename TStruct>
	void AssignTrackedStruct(TStruct& Target, const TStruct& Source)
	{
		ChangedFieldCount += AssignStructAndCountChangedFields(*TStruct::StaticStruct(), &Target, &Source);
	}

	void ApplyLiveMaterialLook(UMaterialInstanceDynamic* Material);

	static int32 AssignStructAndCountChangedFields(const UScriptStruct& Struct, void* Target, const void* Source);

	TSet<FObjectKey> ReappliedMaterialKeys;
};

UCLASS()
class TDGAME_API UTDFluidLiveSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	TSharedPtr<FTDFluidMassInputHub, ESPMode::ThreadSafe> GetMassInputHub();
	void RegisterObstacleSource(UTDFluidObstacleComponent* ObstacleSource);
	void UnregisterObstacleSource(UTDFluidObstacleComponent* ObstacleSource);
	void MarkObstacleSourcesDirty() { bObstacleSourcesDirty = true; }
	double GetLastMassInputReadSeconds() const { return LastMassInputReadSeconds; }
	double GetLastMassInputPublishSeconds() const { return LastMassInputPublishSeconds; }
	int32 GetObstacleSourceCount() const;
	static void SelectNearestObstacles(TConstArrayView<FTDFluidSdfObstacle> SourceObstacles, const FVector& CenterWorld, int32 MaxObstacleCount, TArray<FTDFluidSdfObstacle>& OutObstacles);

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return !RegisteredActors.IsEmpty() || !PendingReleasedGpuActorKeys.IsEmpty() || PendingReapplyTableGeneration != INDEX_NONE; }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintPure, Category = "FluidFX", meta = (WorldContext = "WorldContextObject"))
	static UTDFluidLiveSubsystem* GetFluidLiveSubsystem(const UObject* WorldContextObject);

	void RegisterLiveActor(ATDFluidLiveActor* LiveActor);
	void UnregisterLiveActor(ATDFluidLiveActor* LiveActor);

	void QueueCombatDamage(
		AActor* Instigator,
		AActor* Target,
		float DamageAmount,
		ETDDamageElement Element,
		bool bHasKnownElement,
		bool bWasCritical,
		ETDDamageDelivery Delivery = ETDDamageDelivery::Direct,
		bool bHasHitLocation = false,
		const FVector& HitLocation = FVector::ZeroVector,
		const FVector& HitDirection = FVector::ZeroVector);

	void QueueCombatDeath(AActor* DeadActor, AActor* Killer, const FVector& DeathLocation);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	ATDFluidLiveActor* SpawnPooledFluidActor(
		UTDFluidPresetAsset* PresetAsset,
		const FVector& WorldLocation,
		float ActiveDurationSeconds = 5.0f);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void ReleaseFluidActorToPool(ATDFluidLiveActor* LiveActor);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	int32 InjectCombatBrushStamp(const FTDFluidBrushStamp& Stamp);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	int32 InjectCombatParticleSplash(
		const TArray<FVector>& ParticlePositions,
		const TArray<FVector>& ParticleVelocities,
		float BrushRadiusCm,
		ETDFluidBrushChannel Channel);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	int32 NotifyCombatDamageApplied(
		AActor* Instigator,
		AActor* Target,
		float DamageAmount,
		ETDDamageElement Element,
		bool bWasCritical = false);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	int32 NotifyCombatActorDeath(
		AActor* DeadActor,
		AActor* Killer,
		const FVector& DeathLocation);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	bool BindLandscapeMaterialToLiveActor(ATDFluidLiveActor* SourceActor, UMaterialInstanceDynamic* LandscapeMID);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	int32 BindLandscapeMaterialViaTag(FName LiveActorTag, UMaterialInstanceDynamic* LandscapeMID);
	int32 BroadcastSharedBrushStamps(TConstArrayView<FTDFluidBrushStamp> Stamps);
	void RegisterFieldInputSource(UTDFluidFieldInputComponent* FieldInputSource);
	void UnregisterFieldInputSource(UTDFluidFieldInputComponent* FieldInputSource);

	void RegisterFlowGuideSource(ATDFluidRiverActor* River);
	void UnregisterFlowGuideSource(ATDFluidRiverActor* River);
	void CollectFlowGuidesInBounds(const FBox2D& BoundsXY, TArray<FTDFluidFlowGuide>& OutGuides) const;
	void NotifyWaterSurfacesChanged();
	int32 GetWaterRegistryVersion() const { return WaterRegistryVersion; }
	void SetRainIntensity(float InRainIntensity);
	float GetEffectiveRainIntensity() const;

	int32 GetActiveActorCount() const;
	int32 GetPooledInactiveActorCount() const;

	uint64 AllocateGpuSolverActorKey();
	void ReleaseGpuSolverActorKey(uint64 ActorKey);
	bool ReadGpuActorState(uint64 ActorKey, FTDFluidGpuStateLayout& OutLayout, TArray<FTDFluidStateChannelBytes>& OutChannels, FString& OutError);
	uint32 GetLastGpuSolverFrameNumber() const { return LastGpuSolverFrameNumber; }

	FRotator GetPlaneViewRotation() const;
	void SetPlaneViewRotationOverrideForTests(const FRotator& ViewRotation);
	void ClearPlaneViewRotationOverrideForTests();

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	FTDFluidPresetReapplyReport GetLastReapplyReport() const { return LastReapplyReport; }

	bool ClaimSharedOutput(const UObject* SharedOutput, class UTDFluidLiveComponent* Requester);
	bool IsSharedOutputClaimedBy(const UObject* SharedOutput, const class UTDFluidLiveComponent* Component) const;

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	bool FindFluidSampleAtWorldLocation(const FVector& WorldLocation, FTDFluidCellSample& OutSample) const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FQueuedCombatDamage
	{
		FVector TargetLocation = FVector::ZeroVector;
		FVector HitLocation = FVector::ZeroVector;
		FVector HitDirection = FVector::ForwardVector;
		float DamageAmount = 0.0f;
		ETDDamageElement Element = ETDDamageElement::Physical;
		bool bHasKnownElement = false;
		bool bHasHitLocation = false;
		bool bWasCritical = false;
	};

	struct FQueuedCombatDeath
	{
		FVector DeathLocation = FVector::ZeroVector;
		FVector SplashDirection = FVector::ForwardVector;
		bool bHasKiller = false;
	};

	struct FRegisteredFlowGuide
	{
		TWeakObjectPtr<ATDFluidRiverActor> River;
		FTDFluidFlowGuide Guide;
		FBox2D BoundsXY = FBox2D(ForceInit);
	};

	void HandleLevelAddedOrRemoved(ULevel* ChangedLevel, UWorld* ChangedWorld);
	void HandlePresetTuningReloaded(int32 TableGeneration);
	void ReapplyPendingPresetTuning();

	void StepFluidWorld(float DeltaTime);
	void RefreshPlaneViewRotation();
	static void RunSolverSteps(TConstArrayView<ATDFluidLiveActor*> SolvingActors, float DeltaTime);
	void SubmitGpuSolverSteps(TConstArrayView<ATDFluidLiveActor*> GpuSolvingActors);
	void ReadMassInputDataChannels();
	void PublishMassInputTargets(TConstArrayView<ATDFluidLiveActor*> GpuSolvingActors);
	void DistributeCollectedObstacles();
	void DistributeGpuSolverReadback();
	void ReleaseGpuSolverRenderState();
	void RecordTickCost(float DeltaTime, double TickSeconds);
	void LogTickCostWindow() const;
	void ResetTickCostWindow();
	ATDFluidLiveActor* AcquireInactivePooledActor();
	void HandleDamageAppliedMessage(FGameplayTag Channel, const FTDDamageAppliedMessage& Message);
	void HandleActorDeathMessage(FGameplayTag Channel, const FTDActorDeathMessage& Message);
	void FlushQueuedCombatEvents();
	int32 InjectDamageStamp(
		const FVector& StampLocation,
		const FVector& HitDirection,
		float DamageAmount,
		ETDDamageElement Element,
		bool bWasCritical);
	int32 InjectDeathStamp(const FVector& DeathLocation, const FVector& SplashDirection);
	static ETDFluidBrushChannel ResolveChannelForDamageElement(ETDDamageElement Element);

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ATDFluidLiveActor>> RegisteredActors;

	FGameplayMessageListenerHandle DamageAppliedListenerHandle;
	FGameplayMessageListenerHandle ActorDeathListenerHandle;
	TMap<FObjectKey, FQueuedCombatDamage> QueuedDamageByTarget;
	TMap<FObjectKey, FQueuedCombatDeath> QueuedDeathByActor;
	TArray<FRegisteredFlowGuide> RegisteredFlowGuides;
	FDelegateHandle LevelAddedHandle;
	FDelegateHandle LevelRemovedHandle;
	FDelegateHandle PresetTuningReloadedHandle;
	FTDFluidPresetReapplyReport LastReapplyReport;
	int32 PendingReapplyTableGeneration = INDEX_NONE;
	int32 WaterRegistryVersion = 0;
	float RainIntensity = 0.0f;

	TSharedPtr<FTDFluidGpuSolverRenderState, ESPMode::ThreadSafe> GpuSolverRenderState;
	TSharedPtr<FTDFluidMassInputHub, ESPMode::ThreadSafe> MassInputHub;
	TArray<TWeakObjectPtr<UTDFluidObstacleComponent>> ObstacleSources;
	TArray<TWeakObjectPtr<UTDFluidFieldInputComponent>> FieldInputSources;
	bool bHasDistributedFieldInputs = false;
	void DistributeFieldInputs(float DeltaTime);
	bool bObstacleSourcesDirty = false;
	double LastMassInputReadSeconds = 0.0;
	double LastMassInputPublishSeconds = 0.0;
	TArray<uint64> PendingReleasedGpuActorKeys;
	uint64 LastGpuSolverActorKey = 0;
	uint32 LastGpuSolverFrameNumber = 0;

	FRotator CachedPlaneViewRotation = FRotator(-60.0, 0.0, 0.0);
	TOptional<FRotator> PlaneViewRotationOverride;

	int32 TickCostFrameCount = 0;
	double TickCostTotalSeconds = 0.0;
	double TickCostMaxSeconds = 0.0;
	double TickCostTotalDeltaSeconds = 0.0;

	TMap<FObjectKey, TWeakObjectPtr<class UTDFluidLiveComponent>> SharedOutputClaims;
	TSet<FObjectKey> WarnedSharedOutputs;
};

