#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FluidFX/TDFluidVolumeDriverTypes.h"
#include "TDFluidVolumeDriverComponent.generated.h"

class UHeterogeneousVolumeComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
class UPrimitiveComponent;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class UTexture;
class UTDFluidLiveComponent;
class UTDFluidPresetAsset;
struct FStreamableHandle;
struct FTDFluidPlaneBasis;
struct FTDFluidPresetReapplyReport;

struct FTDFluidVolumePlacement
{
	FTransform Transform;
	FIntVector Resolution = FIntVector::ZeroValue;
};

struct FTDFluidDrivenTarget
{
	TWeakObjectPtr<UMeshComponent> Component;
	ETDFluidOutputMaterialSlot Slot = ETDFluidOutputMaterialSlot::Primary;
	TArray<TWeakObjectPtr<UMaterialInterface>> OriginalMaterials;
};

UCLASS(ClassGroup = (FluidFX))
class TDGAME_API UTDFluidVolumeDriverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTDFluidVolumeDriverComponent();

	void ConfigureFromLiveComponent(UTDFluidLiveComponent& LiveComponent);
	void DriveTargets(const UTDFluidLiveComponent& LiveComponent, float DeltaTime);
	void ReapplyMaterialLooks(FTDFluidPresetReapplyReport& InOutReport);
	void SetDriverHidden(bool bInIsHidden);
	void SetTerrainHeightTexture(UTexture* InTexture, const FVector4f& InWindow, const FVector4f& InDecode);
	int32 CollectTaggedTargets(double NowSeconds, bool bForce);
	void RestoreTargetMaterials();

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	void RequestHeightRecapture();

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	UMaterialInstanceDynamic* GetOutputMaterialInstance(ETDFluidOutputMaterialSlot Slot) const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FTDFluidVolumeDriverSettings GetEffectiveVolumeDriverSettings() const { return DriverSettings; }

	const FTDFluidVolumeDriverSettings& GetDriverSettings() const { return DriverSettings; }
	const FTDFluidOutputTargetSettings& GetOutputTargetSettings() const { return OutputTargetSettings; }
	const FTDFluidTerrainHeightSettings& GetTerrainHeightSettings() const { return TerrainHeightSettings; }
	UHeterogeneousVolumeComponent* GetInternalHeterogeneousVolume() const { return InternalHeterogeneousVolume; }
	UStaticMeshComponent* GetInternalFogVolume() const { return InternalFogVolume; }
	UTextureRenderTarget2D* GetHeightCaptureTarget() const { return HeightCaptureTarget; }
	FVector4f GetTerrainHeightWindow() const { return TerrainHeightWindow; }
	FVector4f GetTerrainHeightDecode() const { return TerrainHeightDecode; }
	int32 GetDrivenTargetCount() const { return DrivenTargets.Num(); }
	int32 GetHeightCaptureCount() const { return HeightCaptureCount; }
	int32 GetTargetCollectionCount() const { return TargetCollectionCount; }

	static FTDFluidVolumePlacement ComputeHeterogeneousVolumePlacement(const FTDFluidPlaneBasis& Basis, const FVector& PlaneCenterWorld, const FVector2D& ExtentCm, const FTDFluidVolumeDriverSettings& Settings);
	static FTransform ComputeFogVolumeTransform(const FBoxSphereBounds& MeshBounds, const FTDFluidPlaneBasis& Basis, const FVector& PlaneCenterWorld, const FVector2D& ExtentCm, const FTDFluidVolumeDriverSettings& Settings);
	static int32 ResolveSlotMaterialIndex(ETDFluidOutputMaterialSlot Slot, const FTDFluidOutputTargetSettings& Settings, int32 MaterialCount);
	static bool ShouldRecollectTargets(double NowSeconds, double LastSeconds, const FTDFluidOutputTargetSettings& Settings, bool bForced);
	static bool ShouldRecaptureHeight(const FVector2D& Center, const FVector2D& LastCenter, float CellCm, int32 Cells, bool bForced);
	static bool IsDriverNeeded(const FTDFluidVolumeDriverSettings& Driver, const FTDFluidTerrainHeightSettings& Terrain, const UTDFluidPresetAsset* Preset);
	static bool IsFogFloorFollowActive(const FTDFluidVolumeDriverSettings& Driver, const FTDFluidTerrainHeightSettings& Terrain, bool bHasHeightTexture, bool bIsWorldFacing);
	static bool IsLandscapeSurfaceComponent(const UPrimitiveComponent& Component);
	static bool ShouldRenderHeightCaptureInMainRenderer(ETDFluidHeightCaptureContent Content, bool bHasGameViewport);
	static bool IsVolumeDomainMaterial(const UMaterialInterface* Material);
	static FVector ComputeWorldOffsetCm(const FTDFluidPlaneBasis& Basis, const FVector& OffsetCm);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	TArray<ETDFluidOutputMaterialSlot> GatherUsedSlots() const;
	void HandleSlotMaterialsLoaded();
	void ApplyLoadedSlotMaterials();
	void ApplySlotMaterialToTargets(ETDFluidOutputMaterialSlot Slot);
	void ApplySlotMaterialToTarget(FTDFluidDrivenTarget& Target, UMaterialInstanceDynamic* SlotMID) const;
	void EnsureInternalComponents();
	void UpdateInternalHeterogeneousVolume(const UTDFluidLiveComponent& LiveComponent, const FVector& PlaneCenterWorld, bool bIsVisible);
	void UpdateInternalFogVolume(const UTDFluidLiveComponent& LiveComponent, const FVector& PlaneCenterWorld, bool bIsVisible);
	void UpdateSceneCapture(const UTDFluidLiveComponent& LiveComponent);
	void ApplyDriverParametersToMID(const UTDFluidLiveComponent& LiveComponent, UMaterialInstanceDynamic& SlotMID) const;
	void ApplyTerrainHeightParametersToMID(UMaterialInstanceDynamic& MID) const;
	void ApplyFogFloorFollowToLiveVolume(const UTDFluidLiveComponent& LiveComponent) const;
	void ClearFogFloorFollowOnLiveVolume() const;
	void ApplyHeightCaptureContent();
	void RestoreDefaultHeightCaptureContent();
	int32 AddTaggedComponents(AActor& TaggedActor, FName Tag, ETDFluidOutputMaterialSlot Slot);
	void ReleaseDriverResources();
	UMaterialInstanceDynamic* FindSlotMID(ETDFluidOutputMaterialSlot Slot) const;

	FTDFluidVolumeDriverSettings DriverSettings;
	FTDFluidOutputTargetSettings OutputTargetSettings;
	FTDFluidTerrainHeightSettings TerrainHeightSettings;

	UPROPERTY(Transient)
	TObjectPtr<UTDFluidPresetAsset> ConfiguredPreset;

	UPROPERTY(Transient)
	TObjectPtr<UHeterogeneousVolumeComponent> InternalHeterogeneousVolume;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> InternalFogVolume;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> HeightCapture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> HeightCaptureTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PrimarySlotMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SecondarySlotMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TertiarySlotMID;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> TerrainHeightTexture;

	TArray<FTDFluidDrivenTarget> DrivenTargets;
	TSharedPtr<FStreamableHandle> SlotMaterialLoadHandle;
	FVector4f TerrainHeightWindow = FVector4f(0.0f, 0.0f, 1.0f, 1.0f);
	FVector4f TerrainHeightDecode = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
	FVector2D LastCaptureCenter = FVector2D::ZeroVector;
	double LastCollectSeconds = 0.0;
	int32 HeightCaptureCount = 0;
	int32 TargetCollectionCount = 0;
	bool bIsDriverHidden = false;
	bool bShouldForceRecollect = true;
	bool bShouldForceRecapture = true;
	bool bHasAcquiredRenderProfile = false;
	bool bHasWarnedNonVolumeMaterial = false;
	bool bHasReleasedResources = false;
};
