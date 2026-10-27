#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TDFluidNiagaraModuleEditorLibrary.generated.h"

UCLASS()
class TDGAMEEDITOR_API UTDFluidNiagaraModuleEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "TD|FluidFX")
	static bool CreateMassInputDataChannel(const FString& PackagePath, FString& OutReport);

	UFUNCTION(BlueprintCallable, Category = "TD|FluidFX")
	static bool CreateMassSplatModuleScript(const FString& PackagePath, FString& OutReport);

	UFUNCTION(BlueprintCallable, Category = "TD|FluidFX")
	static bool CreateDataChannelAppendModuleScript(const FString& PackagePath, const FString& DataChannelPath, FString& OutReport);
};
