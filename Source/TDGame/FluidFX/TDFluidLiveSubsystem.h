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
class UMaterialInstanceDynamic;
class UTDFluidPresetAsset;
struct FTDActorDeathMessage;
struct FTDDamageAppliedMessage;

UCLASS()
class TDGAME_API UTDFluidLiveSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return !RegisteredActors.IsEmpty(); }
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

	int32 GetActiveActorCount() const;
	int32 GetPooledInactiveActorCount() const;

	FRotator GetPlaneViewRotation() const;
	void SetPlaneViewRotationOverrideForTests(const FRotator& ViewRotation);
	void ClearPlaneViewRotationOverrideForTests();

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

	void StepFluidWorld(float DeltaTime);
	void RefreshPlaneViewRotation();
	static void RunSolverSteps(TConstArrayView<ATDFluidLiveActor*> SolvingActors, float DeltaTime);
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

	FRotator CachedPlaneViewRotation = FRotator(-60.0, 0.0, 0.0);
	TOptional<FRotator> PlaneViewRotationOverride;

	int32 TickCostFrameCount = 0;
	double TickCostTotalSeconds = 0.0;
	double TickCostMaxSeconds = 0.0;
	double TickCostTotalDeltaSeconds = 0.0;
};

