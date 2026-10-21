#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "TDFluidEmberComponent.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UTDFluidLiveComponent;
struct FStreamableHandle;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TDGAME_API UTDFluidEmberComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	UTDFluidEmberComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember")
	TSoftObjectPtr<UStaticMesh> EmberMeshAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember")
	TSoftObjectPtr<UMaterialInterface> EmberMaterialAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember", meta = (ClampMin = "1", ClampMax = "512"))
	int32 MaxEmbers = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember", meta = (ClampMin = "0.0"))
	float SpawnPerSecond = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember", meta = (ClampMin = "0.0"))
	float SpawnPerDensityArea = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember")
	FVector2D LifetimeRangeSeconds = FVector2D(1.2, 2.2);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember", meta = (ClampMin = "0.0"))
	float FluidDragPerSec = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember")
	float BuoyancyCmPerSec2 = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember")
	float GravityCmPerSec2 = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember", meta = (ClampMin = "0.0"))
	float CoolingPerSec = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember", meta = (ClampMin = "0.0"))
	float MinSpeedKillCmPerSec = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember", meta = (ClampMin = "0.0"))
	float SlowKillSeconds = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember", meta = (ClampMin = "0.0"))
	float StretchPerCmPerSec = 0.004f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember", meta = (ClampMin = "0.001"))
	float BaseScale = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember", meta = (ClampMin = "0.0"))
	float DepthJitterCm = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BurstUpwardRatio = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ember")
	int32 RandomSeed = 1;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void StepEmbers(const UTDFluidLiveComponent& LiveComponent, float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "Ember")
	void SpawnBurst(FVector Center, int32 Count, float SpeedCmPerSec);

	static FVector MakeBurstDirection(float DirectionZ, float DirectionAngle, float UpwardRatio);

	UFUNCTION(BlueprintPure, Category = "Ember")
	int32 GetAliveEmberCount() const { return AliveEmberCount; }

	UFUNCTION(BlueprintPure, Category = "Ember")
	float GetMeanEmberHeat() const;

	UFUNCTION(BlueprintPure, Category = "Ember")
	int32 ComputeEmberStateHash() const;

private:
	void RequestAsyncAssetLoads();
	void HandleAssetsLoaded();
	void EnsureEmberPool();
	int32 FindFreeEmberSlot() const;
	void SpawnEmbersFromDensity(const UTDFluidLiveComponent& LiveComponent, float DeltaTime);
	void AdvanceEmbers(const UTDFluidLiveComponent& LiveComponent, float DeltaTime);
	void WriteInstances();
	void ActivateEmber(int32 SlotIndex, const FVector& Position, const FVector& Velocity);

	TSharedPtr<FStreamableHandle> AssetLoadHandle;
	TArray<FVector> Positions;
	TArray<FVector> Velocities;
	TArray<float> Heats;
	TArray<float> Ages;
	TArray<float> Lifetimes;
	TArray<float> SlowSeconds;
	TArray<FTransform> InstanceTransforms;
	uint32 RandomState = 0;
	float SpawnCarry = 0.0f;
	int32 AliveEmberCount = 0;
	bool bIsRandomStateReady = false;
};
