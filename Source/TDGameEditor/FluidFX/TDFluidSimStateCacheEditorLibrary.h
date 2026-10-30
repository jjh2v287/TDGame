#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TDFluidSimStateCacheEditorLibrary.generated.h"

class ATDFluidLiveActor;

UCLASS()
class TDGAMEEDITOR_API UTDFluidSimStateCacheEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "TD|FluidFX")
	static FString CaptureSimStateCacheToFile(ATDFluidLiveActor* LiveActor, const FString& OutputDirectory, const FString& CacheName);

	UFUNCTION(BlueprintCallable, Category = "TD|FluidFX")
	static FString CreateSimStateCacheAsset(const FString& SourceJsonPath, const FString& PackagePath, const FString& AssetName, bool bOverwrite);
};
