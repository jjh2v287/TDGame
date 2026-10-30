#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FluidFX/TDFluidCloudTypes.h"
#include "TDFluidCloudLayersActor.generated.h"

class ATDFluidLiveActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture;
class UVolumetricCloudComponent;
struct FStreamableHandle;

UENUM(BlueprintType)
enum class ETDFluidCloudSlotTextureRole : uint8
{
	Coverage,
	Section
};

USTRUCT(BlueprintType)
struct FTDFluidCloudSlotSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud", meta = (ClampMin = "0", ClampMax = "3"))
	int32 SlotIndex = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud")
	ETDFluidCloudSlotTextureRole Role = ETDFluidCloudSlotTextureRole::Coverage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud")
	FName LiveActorTag;
};

USTRUCT(BlueprintType)
struct FTDFluidCloudSetup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud")
	FName SetupName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud")
	TSoftObjectPtr<UMaterialInterface> CloudMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud")
	TArray<FTDFluidCloudSlotSource> SlotSources;
};

struct FTDFluidCloudSlotBand
{
	bool bIsActive = false;
	float BottomKm = 0.0f;
	float ThicknessKm = 0.0f;
	float TerrainFollow = 0.0f;
	float LobeDepthRatio = 0.0f;
};

UCLASS()
class TDGAME_API ATDFluidCloudLayersActor : public AActor
{
	GENERATED_BODY()

public:
	ATDFluidCloudLayersActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cloud")
	TObjectPtr<UVolumetricCloudComponent> CloudComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud")
	TArray<FTDFluidCloudSetup> Setups;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud", meta = (ClampMin = "0"))
	int32 DefaultSetupIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud")
	bool bTakesOverSceneCloud = false;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Cloud")
	FTDFluidCloudSceneSettings GetEffectiveCloudSceneSettings() const { return SceneSettings; }

	static FVector2D ComputeSlabKm(const TArray<FTDFluidCloudSlotBand>& Bands, const FVector2D& TerrainHeightRangeKm);
	static bool ResolveNearSampling(float CameraAltitudeKm, const FVector2D& SlabKm, float EnterMarginKm, float ExitMarginKm, bool bWasNear);
	static int32 ResolveSetupIndex(int32 CVarValue, int32 DefaultIndex, int32 SetupCount);
	static FName MakeSlotParameterName(int32 SlotIndex, const FString& Suffix);
	static bool ShouldWarnWrapMismatch(float Source, float FootprintSizeKmX, bool bIsSourceWrapped);

	int32 GetActiveSetupIndex() const { return ActiveSetupIndex; }
	UMaterialInstanceDynamic* GetActiveMaterial() const { return ActiveMaterial; }
	FVector2D GetActiveSlabKm() const { return ActiveSlabKm; }
	bool IsUsingNearSampling() const { return bIsUsingNearSampling; }
	bool IsYieldingSceneCloud() const { return bIsYieldingSceneCloud; }
	bool HasLoadedCloudMaterials() const { return bHasLoadedCloudMaterials; }

private:
	bool ResolveSceneCloudOwnership();
	void RestoreHiddenSceneClouds();
	void HandleCloudMaterialsLoaded();
	void HandlePresetTuningReloaded(int32 TableGeneration);
	void ReadSceneSettings();
	void ActivateSetup(int32 SetupIndex);
	void RefreshActiveLayer();
	void HideCloudLayer();
	void ApplyTracingSettings();
	void ResolveSourceLiveActors();
	void RetrySourceResolution();
	void BindSlotTextures(bool bForce);
	void WarnIfSlotTilesNonWrappedSource(int32 SlotIndex, const ATDFluidLiveActor& LiveActor);
	void ApplySourceAwake();
	void UpdateViewSampling(bool bForce);
	void WarnIfSetupConsoleValueIsOutOfRange(int32 ConsoleValue);
	bool TryGetFirstLocalCameraLocation(FVector& OutLocation) const;
	bool HasUnresolvedSources() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SetupMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ActiveMaterial;

	TMap<FName, TWeakObjectPtr<ATDFluidLiveActor>> SourceLiveActors;
	TMap<FName, TWeakObjectPtr<UTexture>> BoundSlotTextures;
	TSet<int32> WrapMismatchWarnedSlots;
	TArray<TWeakObjectPtr<UVolumetricCloudComponent>> HiddenSceneClouds;
	FTDFluidCloudSceneSettings SceneSettings;
	TSharedPtr<FStreamableHandle> MaterialLoadHandle;
	FDelegateHandle PresetTuningReloadedHandle;
	FVector2D ActiveSlabKm = FVector2D::ZeroVector;
	int32 ActiveSetupIndex = INDEX_NONE;
	int32 LastWarnedSetupConsoleValue = INDEX_NONE;
	float AppliedViewSampleCountScale = -1.0f;
	double LastSourceResolveSeconds = 0.0;
	bool bIsUsingNearSampling = false;
	bool bIsYieldingSceneCloud = false;
	bool bHasLoadedCloudMaterials = false;
};
