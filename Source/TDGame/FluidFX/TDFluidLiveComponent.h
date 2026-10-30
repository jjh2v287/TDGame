#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureDefines.h"
#include "FluidFX/TDFluidFieldInputComponent.h"
#include "FluidFX/TDFluidGpuSolverProxy.h"
#include "FluidFX/TDFluidRainImpacts.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidNearRippleLayer.h"
#include "FluidFX/TDFluidFoliageBendLayer.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidRuntimeOps.h"
#include "UObject/ObjectKey.h"
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
class UTDFluidDenseWaterCacheAsset;
class UTexture;
class UTexture2D;
class UTextureRenderTarget2D;
struct FStreamableHandle;
struct FTDFluidPresetReapplyReport;
struct FTDFluidSimStateSnapshot;
struct FTDFluidTransmittanceInputs;

struct FTDFluidTileMapTarget
{
	TWeakObjectPtr<UMaterialInstanceDynamic> TargetMID;
	TWeakObjectPtr<UTexture> LastTexture;
	bool bWasActive = false;
};

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiveOutputExternalRenderers")
	FTDFluidExternalOutputTargets ExternalOutputTargets;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void InitSystemStateFromPreset(UTDFluidPresetAsset* InPreset);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void ApplySimulationConfig(const FTDFluidSimulationConfig& InConfig);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void ResetSimulationState();

	void ReapplyPresetTuning(FTDFluidPresetReapplyReport& InOutReport);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void QueueBrushStamp(const FTDFluidBrushStamp& Stamp);
	void QueueNearRippleIdleContact(const FVector& WorldLocation);
	void SubmitNearRippleStep(const FVector2D& CenterWorldXY, float DeltaTime);
	void DiscardNearRippleSources();
	bool IsNearRippleActive() const;
	UTexture* GetNearRippleTexture() const;
	const FTDFluidWaterNearRippleSettings& GetNearRippleSettings() const { return NearRippleSettings; }

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

	void RegisterTileMapTarget(UMaterialInstanceDynamic* TargetMID);

	bool IsAreaWrapped() const;

	static FVector2D ComputeTileMapOriginCm(const FVector2D& AnchorCm, const FVector2D& PeriodCm, const FVector2D& ObserverCm);

	static FVector2D WrapPlanePointIntoWindow(const FVector2D& PlanePoint, const FVector2D& WindowMinimumCm, const FVector2D& WindowExtentCm);

	static void AppendWrappedStampGhosts(TArray<FTDFluidBrushStamp>& InOutStamps, const FVector2D& WindowMinimumCm, const FVector2D& WindowExtentCm);

	static void AppendWrappedObstacleGhosts(TArray<FTDFluidSdfObstacle>& InOutObstacles, const FVector2D& WindowMinimumCm, const FVector2D& WindowExtentCm);

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
	bool IsHeatAuxOutputActive() const;
	bool IsObstacleDistanceAuxActive() const;
	bool IsFoliageBendAuxActive() const;
	bool IsTransmittanceActive() const;
	void SetVolumeTransmittanceSources(const FTDFluidVolumeLightState& LightState, TConstArrayView<FTDFluidShadowCapsule> Capsules);
	const FTDFluidVolumeLightState& GetVolumeLightState() const { return CachedVolumeLightState; }
	const TArray<FTDFluidShadowCapsule>& GetShadowCapsules() const { return CachedShadowCapsules; }
	FTDFluidSimulationConfig MakeCpuSolverConfig() const;
	bool BuildTransmittanceInputs(FTDFluidTransmittanceInputs& OutInputs) const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	UTexture2D* GetWaterSurfaceTexture() const { return WaterSurfaceTexture; }

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void SetWaterProbeLocations(const TArray<FVector>& WorldLocations);

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	bool GetWaterProbeSamples(TArray<FTDFluidWaterPointSample>& OutSamples) const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	UTexture* GetWaterSurfaceOutputTexture() const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	bool IsUsingGpuWaterLayer() const;

	void SetWaterGuidesAndRegions(const TArray<FTDFluidFlowGuide>& Guides, const TArray<FTDFluidWaterRegion>& Regions);
	bool CanReceiveWaterInputs() const;
	int32 GetTerrainSampleResolution() const;
	FVector2D TerrainSampleCellToPlaneCenter(int32 CellX, int32 CellY) const;
	float GetWaterSurfaceHeightAtTerrainSampleCell(int32 CellX, int32 CellY) const;

	void SetWaterLayerDisplayAlpha(float Alpha);
	void SetTerrainReferenceZCm(float InTerrainReferenceZCm) { TerrainReferenceZCm = InTerrainReferenceZCm; }
	bool IsDenseWaterActive() const;
	bool SeedDenseWaterState(const UTDFluidDenseWaterCacheAsset& Cache);
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
	FTDFluidVolumeRenderSettings GetVolumeRenderSettings() const;
	void ApplyParametersToMID(UMaterialInstanceDynamic* TargetMID, float FloorHeightCm = 0.0f) const;
	void SetTerrainCollisionObstacles(const TArray<FTDFluidSdfObstacle>& InObstacles);
	const TArray<FTDFluidSdfObstacle>& GetTerrainCollisionObstacles() const { return TerrainCollisionObstacles; }
	const FTDFluidInputBrushSettings& GetInputBrushSettings() const { return InputBrushSettings; }
	const FTDFluidMassInputSettings& GetMassInputSettings() const { return MassInputSettings; }
	bool AcceptsMassInput() const;
	void QueueMassInputPoints(TConstArrayView<FTDFluidMassInputPoint> Points, ETDFluidBrushSource Source = ETDFluidBrushSource::DataChannel);
	bool BuildMassInputTarget(FTDFluidMassInputTarget& OutTarget) const;
	FTDFluidGpuMassInputCounters GetMassInputCounters() const;
	int32 GetPendingMassPointCount() const { return PendingMassPoints.Num(); }
	int32 GetLastUploadedMassPointCount() const { return LastUploadedMassPointCount; }
	void SetMassInputSettingsForTests(const FTDFluidMassInputSettings& InSettings) { MassInputSettings = InSettings; }
	const FTDFluidRainSettings& GetRainSettings() const { return RainSettings; }
	float GetRainPuddleLevelCm() const { return RainImpactState.PuddleLevelCm; }
	const FTDFluidRainImpactState& GetRainImpactState() const { return RainImpactState; }
	void SetRainSettingsForTests(const FTDFluidRainSettings& InSettings) { RainSettings = InSettings; }
	FVector GetSimulationWindowCenterWorld() const;
	void EnsureMassInputDataChannelLoadRequested();
	void SetCollectedObstacles(const TArray<FTDFluidSdfObstacle>& InObstacles);
	const TArray<FTDFluidSdfObstacle>& GetCollectedObstacles() const { return CollectedObstacles; }
	static bool ProjectObstacleToPlane(const FTDFluidPlaneBasis& Basis, double InPlaneDepthCm, const FTDFluidPlaneSettings& InPlaneSettings, const FTDFluidSdfObstacle& InObstacle, FTDFluidSdfObstacle& OutObstacle);
	static void SortAndClampMassPoints(TArray<FTDFluidMassInputPoint>& InOutPoints, const FVector& CenterWorld, int32 MaxPoints);
	const FTDFluidRuntimeOpsSettings& GetRuntimeOpsSettings() const { return RuntimeOpsSettings; }
	void QueueSimStateSeed(TSharedPtr<const FTDFluidSimStateSnapshot, ESPMode::ThreadSafe> InSeed);
	bool HasPendingSimStateSeed() const { return PendingSimStateSeed.IsValid(); }
	bool CaptureSimStateSnapshot(FTDFluidSimStateSnapshot& OutSnapshot, FString& OutError);
	void ApplyRuntimeConfigOverride(const FTDFluidSimulationConfig& InConfig) { ApplySimulationConfigAndDetectReset(InConfig); }
	void SetWaterDisplayAlphaScale(float InScale) { WaterDisplayAlphaScale = FMath::Clamp(InScale, 0.0f, 1.0f); }
	float GetWaterDisplayAlphaScale() const { return WaterDisplayAlphaScale; }

	const FTDFluidSurfaceRenderSettings& GetSurfaceRenderSettings() const { return SurfaceRenderSettings; }

	bool NeedsStampGroundElevation() const;
	void DriveNiagaraComponent(UNiagaraComponent& NiagaraComp, const AActor& HeightSourceActor) const;

	void SetWetnessSourceComponent(UTDFluidLiveComponent* SourceComponent);
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
	bool TryGetSampledTerrainRangeCm(float& OutMinCm, float& OutMaxCm) const;
	void CompleteTerrainElevationSampling();
	const FTDFluidReadbackField& GetGpuReadbackField() const { return GpuSolverProxy.GetReadbackField(); }
	bool AppendPreparedGpuStep(UTDFluidLiveSubsystem& FluidSubsystem, FTDFluidGpuFrameInputs& InOutFrameInputs);
	void ConsumeGpuReadback(const FTDFluidGpuReadbackFrame& ReadbackFrame);

	const FTDFluidGrid2DSolver& GetSolver() const { return Solver; }
	FTDFluidGrid2DSolver& GetMutableSolver() { return Solver; }
	int32 GetLastProcessedPointCount() const { return LastProcessedPointCount; }
	const TArray<FTDFluidBrushStamp>& GetPendingBrushStamps() const { return PendingBrushStamps; }
	void SetInputBrushSettingsForTests(const FTDFluidInputBrushSettings& InSettings) { InputBrushSettings = InSettings; }
	const FTDFluidFieldInputSettings& GetFieldInputSettings() const { return FieldInputSettings; }
	void SetFieldInputSettingsForTests(const FTDFluidFieldInputSettings& InSettings) { FieldInputSettings = InSettings; }
	void SetFieldInputPlacements(TArray<FTDFluidFieldInputPlacement>&& InPlacements);
	int32 GetIgnoredFieldInputCount() const { return IgnoredFieldInputCount; }
	const TArray<FFloat16Color>& GetPackedDensityPressurePixels() const { return PackedDensityPressurePixels; }
	const TArray<FFloat16Color>& GetPackedVelocityPixels() const { return PackedVelocityPixels; }
	const TArray<FFloat16Color>& GetPackedWaterSurfacePixels() const { return PackedWaterSurfacePixels; }
	const TArray<FFloat16Color>& GetPackedAuxPixels() const { return PackedAuxPixels; }
	const FTDFluidFoliageBendLayer& GetFoliageBendLayer() const { return FoliageBendLayer; }
	const TArray<FFloat16Color>& GetPackedTransmittancePixels() const { return PackedTransmittancePixels; }
	UNiagaraComponent* GetOwnedNiagaraComponent() const { return OwnedNiagaraComponent; }
	bool IsEligibleForVisibilityPause() const;
	void DiscardPendingInputs();

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FTDFluidRuntimeOpsSettings GetEffectiveRuntimeOpsSettings() const { return RuntimeOpsSettings; }

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FTDFluidInputBrushSettings GetEffectiveInputBrushSettings() const { return InputBrushSettings; }

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FTDFluidFieldInputSettings GetEffectiveFieldInputSettings() const { return FieldInputSettings; }

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FTDFluidSurfaceRenderSettings GetEffectiveSurfaceRenderSettings() const { return SurfaceRenderSettings; }

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FTDFluidWaterNearRippleSettings GetEffectiveNearRippleSettings() const { return NearRippleSettings; }

private:
	FTDFluidGpuBackendDecision ResolveSolverBackend() const;
	bool ApplySimulationConfigAndDetectReset(const FTDFluidSimulationConfig& InConfig);
	bool HasFreshEffectivePresetSettings() const;
	void RefreshEffectivePresetSettings(FTDFluidPresetReapplyReport* InOutReport);
	void ApplyGroundSlabToPendingStamps();
	void ApplyTrackParametersToMID(UMaterialInstanceDynamic& TargetMID) const;
	void ReapplyMaterialLooks(FTDFluidPresetReapplyReport& InOutReport);
	void ConsumePendingCpuSimStateSeed();
	void AppendPendingGpuSimStateSeed(FTDFluidGpuFrameInputs& InOutFrameInputs);
	void ApplyNiagaraLookIfStale(UNiagaraComponent& NiagaraComp);
	void InitializeSimulation(const FVector2D& CenterWorld);
	void SyncSolverBackend(const FVector2D& FallbackCenterWorld);
	bool PrepareGpuSimulationStep(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift);
	bool AdvanceCpuSolverWindow(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift);
	bool AdvanceGpuSolverWindow(float DeltaTime, const FVector2D& DesiredCenterWorld, bool bAllowSlidingShift);
	FVector2D ComputeSolverAdvectionOffset(float DeltaTime, const FVector2D& DesiredCenterWorld) const;
	void EnsureNoiseLatticeOffsetCells();
	void EnsureGpuOutputTargets();
	void ClearGpuOutputTargets();
	void ReleaseGpuSolverSlot();
	bool IsWaterLayerEnabled() const;
	bool IsWaterMaterialActive() const;
	void QueueRainForStep(float DeltaTime);
	bool IsRainActive() const;
	float ResolveRainIntensity() const;
	void SubmitGpuWaterProbePlanePoints();
	bool GetCpuWaterProbeSamples(TArray<FTDFluidWaterPointSample>& OutSamples) const;
	bool GetGpuWaterProbeSamples(TArray<FTDFluidWaterPointSample>& OutSamples) const;
	void RequestAsyncAssetLoads();
	void HandleAssetsLoaded();
	void ApplyLoadedOutputsToBoundMesh();
	void ApplyLoadedOutputsToVolumeMesh();
	void FitVolumeMeshToSimulation();
	void ApplyVolumeParameters() const;
	void UploadSolverOutputTextures();
	void ReapplyActiveObstacles();
	bool HasAnySolverObstacles() const;
	void BuildWorldSolverObstacles(TArray<FTDFluidSdfObstacle>& OutObstacles) const;
	void ReinitializeSolverAtOwnerLocation();
	FVector2D ResolveOwnerCenterWorld() const;
	void ApplyInputBrushRulesToPendingStamps();
	double ResolveBrushSurfaceDepthCm() const;
	void ConvertPendingStampsToSolverPlane();
	FVector2D ResolveWrapWindowMinimumCm() const;
	void WrapPendingStampsIntoWindow();
	void WrapObstaclesIntoWindow(TArray<FTDFluidSdfObstacle>& InOutObstacles) const;
	void ApplyTileMapTargets();
	FVector2D ResolveTileMapObserverWorld(const FVector2D& FallbackWorldCm) const;
	TextureAddress ResolveOutputTextureAddress() const;
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
	void SyncOwnedNiagaraComponent();
	void DestroyOwnedNiagaraComponent();
	void RefreshTaggedNiagaraComponentCache(UWorld* World);
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
	TArray<FTDFluidSdfObstacle> TerrainCollisionObstacles;

	UPROPERTY(Transient)
	TArray<FTDFluidSdfObstacle> CollectedObstacles;

	UPROPERTY(Transient)
	FTDFluidMassInputSettings MassInputSettings;

	TArray<FTDFluidGpuMassPointInput> PendingMassPoints;
	FSoftObjectPath RequestedMassInputDataChannelPath;
	int32 LastUploadedMassPointCount = 0;

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
	TObjectPtr<UTextureRenderTarget2D> NearRippleTarget;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> GpuTransmittanceTarget;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> GpuWaterSurfaceTarget;

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
	TArray<FTDFluidTileMapTarget> TileMapTargets;
	TArray<TWeakObjectPtr<UNiagaraComponent>> CachedTaggedNiagaraComponents;
	FName CachedNiagaraActorTag = NAME_None;
	double LastNiagaraCacheRefreshSeconds = -1.0e9;
	TSharedPtr<FStreamableHandle> AssetLoadHandle;

	TArray<FFloat16Color> PackedDensityPressurePixels;
	TArray<FFloat16Color> PackedVelocityPixels;
	TArray<FFloat16Color> PackedWaterSurfacePixels;
	TArray<FFloat16Color> PackedAuxPixels;
	int32 PackedAuxObstacleGeneration = INDEX_NONE;
	FTDFluidNearRippleLayer NearRippleLayer;
	FTDFluidWaterNearRippleSettings NearRippleSettings;
	FTDFluidFoliageBendLayer FoliageBendLayer;
	float PreparedFoliageBendDeltaSeconds = 0.0f;
	bool bIsFoliageBendStepEnabled = false;
	void RefreshFoliageBendStep(float DeltaTime);
	void AdvanceFoliageBendCpu(float DeltaTime);
	void SubmitFoliageBendGpu();
	TArray<FFloat16Color> PackedTransmittancePixels;
	FTDFluidVolumeLightState CachedVolumeLightState;
	FTDFluidPlaneSettings EffectivePlaneSettings;
	FTDFluidVolumeRenderSettings EffectiveVolumeRender;
	FTDFluidFieldInputSettings FieldInputSettings;
	TArray<FTDFluidFieldInputPlacement> PendingFieldInputPlacements;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture>> FieldInputTextures;
	int32 IgnoredFieldInputCount = 0;
	bool bHasWarnedIgnoredFieldInputs = false;
	TArray<FTDFluidGpuFieldInput> BuildGpuFieldInputs();
	void RecordIgnoredFieldInputs();
	FTDFluidInputBrushSettings InputBrushSettings;
	FTDFluidRuntimeOpsSettings RuntimeOpsSettings;
	TSharedPtr<const FTDFluidSimStateSnapshot, ESPMode::ThreadSafe> PendingSimStateSeed;
	float WaterDisplayAlphaScale = 1.0f;
	FTDFluidSurfaceRenderSettings SurfaceRenderSettings;
	TWeakObjectPtr<UTDFluidLiveComponent> WetnessSourceComponent;
	bool bHasWetnessSourceBinding = false;
	TWeakObjectPtr<UTDFluidPresetAsset> EffectiveSettingsPreset;
	ETDFluidPresetKind EffectiveSettingsPresetKind = ETDFluidPresetKind::Custom;
	int32 TableGeneration = INDEX_NONE;
	TMap<FObjectKey, int32> NiagaraLookGenerationByComponent;
	TArray<FTDFluidShadowCapsule> CachedShadowCapsules;
	TArray<FVector> WaterProbeWorldLocations;
	int32 WaterProbeGeneration = 0;
	TArray<FTDFluidFlowGuide> LastWaterGuides;
	TArray<FTDFluidWaterRegion> LastWaterRegions;
	bool bHasLastWaterInputs = false;
	bool bHasEffectivePresetSettings = false;
	bool bIsSimulationConfigFromPreset = false;
	bool bHasWaterInputsMissingOnCpu = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "FluidFX", meta = (AllowPrivateAccess = "true"))
	FTDFluidRainSettings RainSettings;

	FTDFluidRainImpactState RainImpactState;

	bool bIsSimulationAwake = true;
	bool bHasPendingObstacleUpdate = false;
	bool bUsesGpuSolver = false;
	bool bHasSampledTerrainRange = false;
	bool bHasPreviousAnchoredCenter = false;
	bool bHasNoiseLatticeOffsetCells = false;
	FVector2D PreviousAnchoredCenterPlane = FVector2D::ZeroVector;
	FIntPoint NoiseLatticeOffsetCells = FIntPoint::ZeroValue;
	int32 LastCpuReinitializationGeneration = INDEX_NONE;
	int32 LastProcessedPointCount = 0;
	float WaterLayerDisplayAlpha = 0.0f;
	float TerrainReferenceZCm = 0.0f;
	float SampledTerrainMinCm = 0.0f;
	float SampledTerrainMaxCm = 0.0f;

	FTDFluidSimulationConfig MakeGpuSolverConfig(int32 GpuResolution) const;
	bool CanKeepSolverStateOnReinit(const FTDFluidSimulationConfig& PreviousConfig, bool bWasInitialized) const;
	bool TryBindExternalOutputTarget(TObjectPtr<UTextureRenderTarget2D>& InOutTarget, const TSoftObjectPtr<UTextureRenderTarget2D>& ExternalTarget, int32 Resolution, const FLinearColor& ExpectedClearColor, TextureAddress ExpectedAddress);
	bool IsUsableClaimedExternalTarget(UTextureRenderTarget2D* Target, int32 Resolution, const FLinearColor& ExpectedClearColor, TextureAddress ExpectedAddress);
	void CopyCpuOutputsToExternalTargets();
	bool ClaimSharedOutput(const UObject* SharedOutput);
	bool ShouldWriteSharedParameterCollection(const UObject* SharedParameterCollection);
	bool IsExternalOutputTarget(const UTextureRenderTarget2D* Target) const;
	bool CanClearGpuOutputTarget(const UTextureRenderTarget2D* Target) const;
	void AppendExternalOutputTargetPaths(TArray<FSoftObjectPath>& InOutPaths) const;

	TSet<FObjectKey> WarnedExternalOutputTargets;
};
