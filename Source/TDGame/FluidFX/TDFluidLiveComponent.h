#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FluidFX/TDFluidGpuSolverProxy.h"
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
class UTDFluidLiveSubsystem;
class UTDFluidPresetAsset;
class UTexture;
class UTexture2D;
class UTextureRenderTarget2D;
struct FStreamableHandle;
struct FTDFluidTransmittanceInputs;

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
	UTexture* GetDensityPressureTexture() const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	UTexture* GetVelocityTexture() const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	UTexture* GetAuxTexture() const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	UTexture* GetTransmittanceTexture() const;

	bool IsAuxOutputActive() const;
	bool IsTransmittanceActive() const;
	void SetVolumeTransmittanceSources(const FTDFluidVolumeLightState& LightState, TConstArrayView<FTDFluidShadowCapsule> Capsules);
	const FTDFluidVolumeLightState& GetVolumeLightState() const { return CachedVolumeLightState; }
	const TArray<FTDFluidShadowCapsule>& GetShadowCapsules() const { return CachedShadowCapsules; }
	FTDFluidSimulationConfig MakeCpuSolverConfig() const;
	bool BuildTransmittanceInputs(FTDFluidTransmittanceInputs& OutInputs) const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	UTexture2D* GetWaterSurfaceTexture() const { return WaterSurfaceTexture; }

	void SetWaterLayerDisplayAlpha(float Alpha);
	void SetTerrainReferenceZCm(float InTerrainReferenceZCm) { TerrainReferenceZCm = InTerrainReferenceZCm; }
	float GetWaterLayerDisplayAlpha() const { return WaterLayerDisplayAlpha; }

	void AdvanceSimulationStep(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift);
	bool PrepareSimulationStep(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift);
	void RunPreparedSolverStep(float DeltaTime);
	void FinishSimulationStep();
	void PublishCurrentOutputs();

	bool SetPlaneBasis(const FTDFluidPlaneBasis& InBasis, const FVector& PivotWorld, double DepthBiasCm);
	const FTDFluidPlaneBasis& GetPlaneBasis() const { return PlaneBasis; }
	double GetPlaneDepthCm() const { return PlaneDepthCm; }
	FTDFluidPlaneSettings GetPlaneSettings() const;
	FVector2D ProjectWorldToSolverPlane(const FVector& WorldPoint) const;
	bool TryProjectWorldPointIntoSlab(const FVector& WorldPoint, FVector2D& OutPlanePoint) const;
	FVector UnprojectSolverPlaneToWorld(const FVector2D& PlanePoint, double DepthOffsetCm = 0.0) const;
	bool ComputeDensityCentroidPlane(bool bUseSelectiveDensity, FVector2D& OutCentroid, float& OutAreaCm2) const;

	bool IsUsingGpuSolver() const { return bUsesGpuSolver; }
	bool IsSimulationInitialized() const;
	int32 GetSimulationResolution() const;
	FVector2D GetSimulationExtentCm() const;
	FVector2D GetSimulationCenterWorld() const;
	int32 GetSimulationGeneration() const;
	FVector2D CellToPlaneCenter(int32 CellX, int32 CellY) const;
	FTDFluidCellSample SampleSimulationCell(int32 CellX, int32 CellY) const;
	void SetTerrainElevationAtCell(int32 CellX, int32 CellY, float ElevationCm);
	void CompleteTerrainElevationSampling();
	const FTDFluidReadbackField& GetGpuReadbackField() const { return GpuSolverProxy.GetReadbackField(); }
	bool AppendPreparedGpuStep(UTDFluidLiveSubsystem& FluidSubsystem, FTDFluidGpuFrameInputs& InOutFrameInputs);
	void ConsumeGpuReadback(const FTDFluidGpuReadbackFrame& ReadbackFrame);

	const FTDFluidGrid2DSolver& GetSolver() const { return Solver; }
	FTDFluidGrid2DSolver& GetMutableSolver() { return Solver; }
	int32 GetLastProcessedPointCount() const { return LastProcessedPointCount; }
	const TArray<FFloat16Color>& GetPackedDensityPressurePixels() const { return PackedDensityPressurePixels; }
	const TArray<FFloat16Color>& GetPackedVelocityPixels() const { return PackedVelocityPixels; }
	const TArray<FFloat16Color>& GetPackedWaterSurfacePixels() const { return PackedWaterSurfacePixels; }
	const TArray<FFloat16Color>& GetPackedAuxPixels() const { return PackedAuxPixels; }
	const TArray<FFloat16Color>& GetPackedTransmittancePixels() const { return PackedTransmittancePixels; }
	UNiagaraComponent* GetOwnedNiagaraComponent() const { return OwnedNiagaraComponent; }

private:
	FTDFluidGpuBackendDecision ResolveSolverBackend() const;
	void InitializeSimulation(const FVector2D& CenterWorld);
	void SyncSolverBackend(const FVector2D& FallbackCenterWorld);
	bool PrepareGpuSimulationStep(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift);
	void EnsureGpuOutputTargets();
	void ClearGpuOutputTargets();
	void ReleaseGpuSolverSlot();
	bool IsWaterLayerEnabled() const;
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
	void PackWaterSurfaceOutputs();
	void RefreshMaterialTargets();
	void ApplyParametersToExternalTargets();
	void PushDirectDriveOutputs();
	void DriveTaggedNiagaraComponents(UWorld* World);
	void DriveOwnedNiagaraComponent();
	void DriveNiagaraComponent(UNiagaraComponent& NiagaraComp, const AActor& HeightSourceActor) const;
	void SyncOwnedNiagaraComponent();
	void DestroyOwnedNiagaraComponent();
	void RefreshTaggedNiagaraComponentCache(UWorld* World);
	void ApplyParametersToMID(UMaterialInstanceDynamic* TargetMID, float FloorHeightCm = 0.0f) const;
	void PackAuxAndTransmittanceOutputs();
	FTDFluidGpuTransmittanceInput BuildGpuTransmittanceInput() const;
	void ApplyVolumeRenderParameters() const;
	void UploadPixelsToSizedTexture(UTexture2D* TargetTexture, const TArray<FFloat16Color>& SourcePixels, int32 TextureSize);
	FVector ToPlaneCoordinates(const FVector& WorldVector) const;
	FVector ToSolverPlanePoint(const FVector& WorldPoint) const;
	FVector ComputeVolumeBoxMinimumPlane() const;

	FTDFluidGrid2DSolver Solver;
	FTDFluidGpuSolverProxy GpuSolverProxy;
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
	TObjectPtr<UTexture2D> WaterSurfaceTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> GpuDensityPressureTarget;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> GpuVelocityTarget;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> AuxTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TransmittanceTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> GpuAuxTarget;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> GpuTransmittanceTarget;

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
	TArray<FFloat16Color> PackedWaterSurfacePixels;
	TArray<FFloat16Color> PackedAuxPixels;
	TArray<FFloat16Color> PackedTransmittancePixels;
	FTDFluidVolumeLightState CachedVolumeLightState;
	TArray<FTDFluidShadowCapsule> CachedShadowCapsules;

	bool bIsSimulationAwake = true;
	bool bHasPendingObstacleUpdate = false;
	bool bUsesGpuSolver = false;
	int32 LastProcessedPointCount = 0;
	float WaterLayerDisplayAlpha = 0.0f;
	float TerrainReferenceZCm = 0.0f;
};
