#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TDFluidWaterMeshEditorLibrary.generated.h"

class UMaterialInterface;
class UStaticMesh;

UCLASS()
class TDGAMEEDITOR_API UTDFluidWaterMeshEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "TD|FluidFX")
	static UStaticMesh* CreateGridStaticMesh(const FString& PackagePath, const FString& AssetName, float SizeCm, int32 QuadsPerSide, UMaterialInterface* SlotMaterial);
};
