#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TDFluidFractureEditorLibrary.generated.h"

class UGeometryCollection;
class UStaticMesh;

UCLASS()
class TDGAMEEDITOR_API UTDFluidFractureEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "TD|FluidFX")
	static UGeometryCollection* CreateBrickGeometryCollection(UStaticMesh* BrickMesh, const FString& PackagePath, const FString& AssetName, FIntVector BrickCount, float BrickSizeCm);
};
