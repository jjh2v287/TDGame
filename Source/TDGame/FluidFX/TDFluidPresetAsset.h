#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FluidFX/TDFluidFireTypes.h"
#include "FluidFX/TDFluidPlaneTypes.h"
#include "FluidFX/TDFluidTypes.h"
#include "FluidFX/TDFluidVolumeDriverTypes.h"
#include "TDFluidPresetAsset.generated.h"

class UMaterialInterface;
class UMaterialParameterCollection;
class UNiagaraSystem;
class UStaticMesh;

UCLASS(BlueprintType)
class TDGAME_API UTDFluidPresetAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	ETDFluidPresetKind PresetKind = ETDFluidPresetKind::Water;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	bool bOverridePresetDefaults = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	FTDFluidSimulationConfig SimulationConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	ETDFluidBrushChannel DefaultChannel = ETDFluidBrushChannel::ChannelA_Water;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	TSoftObjectPtr<UMaterialInterface> OutputMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	TSoftObjectPtr<UStaticMesh> SurfaceMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	TSoftObjectPtr<UNiagaraSystem> ExternalNiagaraSystem;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	TSoftObjectPtr<UMaterialParameterCollection> ParameterCollection;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	FName ExternalRendererActorTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	TSoftObjectPtr<UMaterialInterface> VolumeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	TSoftObjectPtr<UStaticMesh> VolumeMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output", meta = (ClampMin = "1.0"))
	float VolumeHeightCm = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	FTDFluidPlaneSettings PlaneSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output", meta = (ClampMin = "-100", ClampMax = "100"))
	int32 TranslucencySortPriority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	FTDFluidVolumeRenderSettings VolumeRender;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	TArray<TSoftObjectPtr<UMaterialInterface>> PrimaryOutputMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	TArray<TSoftObjectPtr<UMaterialInterface>> SecondaryOutputMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	TArray<TSoftObjectPtr<UMaterialInterface>> TertiaryOutputMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	FName PrimaryOutputActorTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	FName SecondaryOutputActorTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	FName TertiaryOutputActorTag = NAME_None;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FTDFluidSimulationConfig ResolveEffectiveConfig() const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FTDFluidPlaneSettings ResolveEffectivePlaneSettings() const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FTDFluidVolumeRenderSettings ResolveEffectiveVolumeRender() const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	bool UsesOwnPresetCopies() const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidSimulationConfig BuildConfigForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidSourceEmitterSettings BuildSourceEmitterForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidFragmentTrailSettings BuildFragmentTrailForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidVolumeRenderSettings BuildVolumeRenderForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidFlameCardLayerSettings BuildFlameCardLayersForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidPlaneSettings BuildPlaneSettingsForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidDownstreamFeedSettings BuildDownstreamFeedForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidFragmentGlowSettings BuildFragmentGlowForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidInputBrushSettings BuildInputBrushForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidRuntimeOpsSettings BuildRuntimeOpsForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidEmberSettings BuildEmberSettingsForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidFireLightSettings BuildFireLightSettingsForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidWaterLodGridSettings BuildWaterLodGridSettingsForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidWaterNearRippleSettings BuildWaterNearRippleSettingsForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidVolumeDriverSettings BuildVolumeDriverForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidOutputTargetSettings BuildOutputTargetForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidTerrainHeightSettings BuildTerrainHeightForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidMassInputSettings BuildMassInputForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidSurfaceRenderSettings BuildSurfaceRenderForPresetKind(ETDFluidPresetKind Kind);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static bool IsFireFamilyPresetKind(ETDFluidPresetKind Kind);

	TSoftObjectPtr<UMaterialInterface> ResolveSlotMaterial(ETDFluidOutputMaterialSlot Slot, int32 MaterialIndex) const;

	FName GetSlotActorTag(ETDFluidOutputMaterialSlot Slot) const;

	static UTDFluidPresetAsset* CreateTransientPreset(UObject* Outer, ETDFluidPresetKind Kind);
};
