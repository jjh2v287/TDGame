#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FluidFX/TDFluidPlaneTypes.h"
#include "FluidFX/TDFluidTypes.h"
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

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FTDFluidSimulationConfig ResolveEffectiveConfig() const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	static FTDFluidSimulationConfig BuildConfigForPresetKind(ETDFluidPresetKind Kind);

	static UTDFluidPresetAsset* CreateTransientPreset(UObject* Outer, ETDFluidPresetKind Kind);
};
