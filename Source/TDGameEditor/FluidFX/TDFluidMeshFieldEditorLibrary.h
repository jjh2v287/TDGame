#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/Float16Color.h"
#include "TDFluidMeshFieldEditorLibrary.generated.h"

class UStaticMesh;

UCLASS()
class TDGAMEEDITOR_API UTDFluidMeshFieldEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "TD|FluidFX")
	static FString BakeMeshSliceField(UStaticMesh* Mesh, float SliceHeightLocalCm, int32 Resolution, float PaddingCm, const FString& TextureAssetPath);

	static FBox2D ResolveSliceBounds(TConstArrayView<FVector3f> Positions, float PaddingCm);
	static bool ComputeSliceSignedDistance(TConstArrayView<FVector3f> Positions, TConstArrayView<int32> TriangleIndices, float SliceHeightCm, const FBox2D& BoundsCm, int32 Resolution, TArray<float>& OutDistanceCm, TArray<FVector2f>& OutOutwardNormal);
	static void PackSliceFieldPixels(TConstArrayView<float> DistanceCm, TConstArrayView<FVector2f> OutwardNormal, TArray<FFloat16Color>& OutPixels);
};
