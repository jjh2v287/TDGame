#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidPresetTuningSubsystem.generated.h"

class FJsonObject;
class IConsoleObject;

struct FTDFluidPresetTuningTable
{
	TMap<ETDFluidPresetKind, TMap<const UScriptStruct*, TArray<TSharedPtr<FJsonObject>>>> LayersByKind;
};

UCLASS()
class TDGAME_API UTDFluidPresetTuningSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	int32 ReloadPresetTuning();

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	TArray<FString> GetLoadErrors() const;

	static FString GetTuningFilePath();
	static bool ParsePresetTuningText(const FString& Text, FTDFluidPresetTuningTable& OutTable, TArray<FString>& OutErrors);
	static void ApplyPresetTuning(ETDFluidPresetKind Kind, const UScriptStruct* Struct, void* OutStruct);

private:
	void ReportFirstLoadFailure() const;

	FTDFluidPresetTuningTable Table;
	TArray<FString> LoadErrors;
	bool bHasTable = false;
	IConsoleObject* ReloadCommand = nullptr;
};
