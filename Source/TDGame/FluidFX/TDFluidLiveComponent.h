#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidLiveComponent.generated.h"

class AActor;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UNiagaraComponent;
class UNiagaraSystem;
class UStaticMeshComponent;
class UTDFluidPresetAsset;
class UTexture2D;
struct FStreamableHandle;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TDGAME_API UTDFluidLiveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTDFluidLiveComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveCore")
	bool bShouldInitSystemStateFromPreset = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveCore")
	TObjectPtr<UTDFluidPresetAsset> ActivePreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveSimulation")
	FTDFluidSimulationConfig SimulationConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveOutputMaterials")
	TSoftObjectPtr<UMaterialInterface> OutputMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveOutputParams")
	TSoftObjectPtr<UMaterialParameterCollection> ParameterCollection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveOutputExternalRenderers")
	TSoftObjectPtr<UNiagaraSystem> ExternalNiagaraSystem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveOutputExternalRenderers")
	FName DriveNiagaraComponentInTaggedActors = NAME_None;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void InitSystemStateFromPreset(UTDFluidPresetAsset* InPreset);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void ApplySimulationConfig(const FTDFluidSimulationConfig& InConfig);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void ResetSimulationState();

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void QueueBrushStamp(const FTDFluidBrushStamp& Stamp);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void QueueNiagaraParticleStamps(
		const TArray<FVector>& ParticlePositions,
		const TArray<FVector>& ParticleVelocities,
		float BrushRadiusCm,
		ETDFluidBrushChannel Channel);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void SetSdfObstacles(const TArray<FTDFluidSdfObstacle>& InObstacles);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void SetSimulationAwake(bool bShouldWake);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	bool IsSimulationAwake() const { return bIsSimulationAwake; }

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void RegisterExternalMaterialTarget(UMaterialInstanceDynamic* TargetMID);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void BindInternalMeshComponent(UStaticMeshComponent* InMeshComponent);

	void BindVolumeMeshComponent(UStaticMeshComponent* InMeshComponent);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	bool SampleFluidAtWorldLocation(const FVector& WorldLocation, FTDFluidCellSample& OutSample) const;

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	bool ComputeDensityCentroidWorld(bool bUseSelectiveDensity, FVector& OutCentroidWorld, float& OutDensityAreaCm2) const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	UTexture2D* GetDensityPressureTexture() const { return DensityPressureTexture; }

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	UTexture2D* GetVelocityTexture() const { return VelocityTexture; }

	void AdvanceSimulationStep(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift);
	bool PrepareSimulationStep(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift);
	void RunPreparedSolverStep(float DeltaTime);
	void FinishSimulationStep();
	void PublishCurrentOutputs();

	bool SetPlaneBasis(const FTDFluidPlaneBasis& InBasis, double InPlaneDepthCm);
	const FTDFluidPlaneBasis& GetPlaneBasis() const { return PlaneBasis; }
	double GetPlaneDepthCm() const { return PlaneDepthCm; }
	FTDFluidPlaneSettings GetPlaneSettings() const;
	FVector2D ProjectWorldToSolverPlane(const FVector& WorldPoint) const;
	bool TryProjectWorldPointIntoSlab(const FVector& WorldPoint, FVector2D& OutPlanePoint) const;
	FVector UnprojectSolverPlaneToWorld(const FVector2D& PlanePoint, double DepthOffsetCm = 0.0) const;
	bool ComputeDensityCentroidPlane(bool bUseSelectiveDensity, FVector2D& OutCentroid, float& OutAreaCm2) const;

	const FTDFluidGrid2DSolver& GetSolver() const { return Solver; }
	FTDFluidGrid2DSolver& GetMutableSolver() { return Solver; }
	int32 GetLastProcessedPointCount() const { return LastProcessedPointCount; }
	const TArray<FFloat16Color>& GetPackedDensityPressurePixels() const { return PackedDensityPressurePixels; }
	const TArray<FFloat16Color>& GetPackedVelocityPixels() const { return PackedVelocityPixels; }
	UNiagaraComponent* GetOwnedNiagaraComponent() const { return OwnedNiagaraComponent; }

private:
	void RequestAsyncAssetLoads();
	void HandleAssetsLoaded();
	void ApplyLoadedOutputsToBoundMesh();
	void ApplyLoadedOutputsToVolumeMesh();
	void FitVolumeMeshToSimulation();
	void ApplyVolumeParameters() const;
	void UploadSolverOutputTextures();
	void ReapplyActiveObstacles();
	void ReinitializeSolverAtOwnerLocation();
	FVector2D ResolveOwnerCenterWorld() const;
	void ConvertPendingStampsToSolverPlane();
	void BuildSolverPlaneObstacles(TArray<FTDFluidSdfObstacle>& OutObstacles) const;
	void RasterizeActiveObstacles();
	void ApplyPlaneRenderFlags();
	void PrioritizeAndClampPendingStamps();
	void EnsureOutputTextures();
	void UploadPixelsToTexture(UTexture2D* TargetTexture, const TArray<FFloat16Color>& SourcePixels);
	void PushDirectDriveOutputs();
	void DriveTaggedNiagaraComponents(UWorld* World);
	void DriveOwnedNiagaraComponent();
	void DriveNiagaraComponent(UNiagaraComponent& NiagaraComp, const AActor& HeightSourceActor) const;
	void SyncOwnedNiagaraComponent();
	void DestroyOwnedNiagaraComponent();
	void RefreshTaggedNiagaraComponentCache(UWorld* World);
	void ApplyParametersToMID(UMaterialInstanceDynamic* TargetMID, float FloorHeightCm = 0.0f) const;

	FTDFluidGrid2DSolver Solver;
	FTDFluidPlaneBasis PlaneBasis;
	double PlaneDepthCm = 0.0;

	UPROPERTY(Transient)
	TArray<FTDFluidBrushStamp> PendingBrushStamps;

	UPROPERTY(Transient)
	TArray<FTDFluidSdfObstacle> ActiveObstacles;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DensityPressureTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> VelocityTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> InternalSurfaceMID;

	UPROPERTY(Transient)
	TWeakObjectPtr<UStaticMeshComponent> BoundInternalMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> VolumeMID;

	UPROPERTY(Transient)
	TWeakObjectPtr<UStaticMeshComponent> BoundVolumeMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> OwnedNiagaraComponent;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> OwnedNiagaraSystem;

	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> ExternalMaterialTargets;
	TArray<TWeakObjectPtr<UNiagaraComponent>> CachedTaggedNiagaraComponents;
	FName CachedNiagaraActorTag = NAME_None;
	double LastNiagaraCacheRefreshSeconds = -1.0e9;
	TSharedPtr<FStreamableHandle> AssetLoadHandle;

	TArray<FFloat16Color> PackedDensityPressurePixels;
	TArray<FFloat16Color> PackedVelocityPixels;

	bool bIsSimulationAwake = true;
	bool bHasPendingObstacleUpdate = false;
	int32 LastProcessedPointCount = 0;
};
