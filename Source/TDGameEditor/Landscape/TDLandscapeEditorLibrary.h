#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "TDLandscapeEditorLibrary.generated.h"

class ALandscape;
class ALandscapeProxy;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FTDLandscapeCreateRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Scale = FVector(100.0, 100.0, 100.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ComponentCountX = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ComponentCountY = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 SectionsPerComponent = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 QuadsPerSection = 63;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString HeightmapRawPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FName> LayerNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> LayerRawPaths;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString LayerInfoPackagePath = TEXT("/Game/World/Landscape/Layers");

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 WorldPartitionGridSizeInComponents = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnableNanite = true;
};

UCLASS()
class TDGAMEEDITOR_API UTDLandscapeEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "TD|Landscape", meta = (WorldContext = "WorldContextObject"))
	static ALandscape* CreateLandscapeFromRawFiles(UObject* WorldContextObject, const FTDLandscapeCreateRequest& Request);

	UFUNCTION(BlueprintCallable, Category = "TD|Landscape")
	static int32 GetExpectedVertexCountPerSide(const FTDLandscapeCreateRequest& Request);

	UFUNCTION(BlueprintCallable, Category = "TD|Landscape")
	static bool TryGetLandscapeHeightAtLocation(ALandscapeProxy* Landscape, FVector Location, float& OutHeight);

	UFUNCTION(BlueprintCallable, Category = "TD|Landscape", meta = (WorldContext = "WorldContextObject"))
	static int32 DestroyAllLandscapeActors(UObject* WorldContextObject);
};
