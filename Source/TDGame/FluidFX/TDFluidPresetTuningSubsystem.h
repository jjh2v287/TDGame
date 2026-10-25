#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/EngineSubsystem.h"
#include "FluidFX/TDFluidTypes.h"
#include "TDFluidPresetTuningSubsystem.generated.h"

class FJsonObject;
class IConsoleObject;
class UMaterialInstanceDynamic;
class UWorld;

UENUM()
enum class ETDFluidPresetTuningScope : uint8
{
	PresetKind,
	Global
};

struct FTDFluidPresetTuningRegisteredStruct
{
	const UScriptStruct* Struct = nullptr;
	ETDFluidPresetTuningScope Scope = ETDFluidPresetTuningScope::PresetKind;
};

struct FTDFluidPresetTuningTable
{
	TMap<ETDFluidPresetKind, TMap<const UScriptStruct*, TArray<TSharedPtr<FJsonObject>>>> LayersByKind;
	TMap<const UScriptStruct*, TSharedPtr<FJsonObject>> GlobalLayers;
};

struct FTDFluidMaterialLookEntry
{
	FString ParentPath;
	TMap<FName, float> Scalars;
	TMap<FName, FLinearColor> Vectors;
	TMap<FName, bool> Switches;
};

struct FTDFluidNiagaraLookEntry
{
	TMap<FName, float> Floats;
	TMap<FName, int32> Ints;
	TMap<FName, bool> Bools;
	TMap<FName, FVector2D> Vectors2;
	TMap<FName, FVector> Vectors3;
	TMap<FName, FLinearColor> Colors;
};

struct FTDFluidMaterialLookTable
{
	TMap<FName, FTDFluidMaterialLookEntry> MaterialsByPackage;
	TMap<FName, FTDFluidNiagaraLookEntry> NiagaraSystemsByPackage;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FTDFluidOnPresetTuningReloaded, int32);

UCLASS()
class TDGAME_API UTDFluidPresetTuningSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	int32 ReloadPresetTuning();

	UFUNCTION(BlueprintCallable, Category = "FluidFX")
	int32 ReloadPresetTuningFromPaths(const FString& TuningPath, const FString& LookPath);

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	TArray<FString> GetLoadErrors() const;

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	int32 GetTableGeneration() const { return TableGeneration; }

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FString GetTuningSourcePath() const { return TuningSourcePath; }

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	FString GetLookSourcePath() const { return LookSourcePath; }

	UFUNCTION(BlueprintPure, Category = "FluidFX")
	bool IsUsingOverridePaths() const;

	FTDFluidOnPresetTuningReloaded OnPresetTuningReloaded;

	static const TArray<FTDFluidPresetTuningRegisteredStruct>& GetRegisteredStructs();
	static FString GetTuningFilePath();
	static FString GetMaterialLookFilePath();
	static bool IsAllowedOverridePath(const FString& FullPath);
	static bool ParsePresetTuningText(const FString& Text, FTDFluidPresetTuningTable& OutTable, TArray<FString>& OutErrors);
	static bool ParsePresetTuningTextWithRegistry(const FString& Text, const TArray<FTDFluidPresetTuningRegisteredStruct>& Registry, FTDFluidPresetTuningTable& OutTable, TArray<FString>& OutErrors);
	static bool ParseMaterialLookText(const FString& Text, FTDFluidMaterialLookTable& OutTable, TArray<FString>& OutErrors);
	static void ApplyPresetTuning(ETDFluidPresetKind Kind, const UScriptStruct* Struct, void* OutStruct);
	static void ApplyPresetTuningFromTable(const FTDFluidPresetTuningTable& SourceTable, ETDFluidPresetKind Kind, const UScriptStruct* Struct, void* OutStruct);
	static const FTDFluidMaterialLookEntry* FindMaterialLook(FName PackageName);
	static const FTDFluidNiagaraLookEntry* FindNiagaraLook(FName PackageName);
	static int32 ApplyMaterialLook(UMaterialInstanceDynamic& Material);
	static int32 CopyMatchingProperties(const UScriptStruct* SourceStruct, const void* Source, UObject& Target);

private:
	int32 LoadTables(const FString& TuningPath, const FString& LookPath);
	void ResolveBasePaths();
	void RememberBaseFileTimestamps();
	void ReportFirstLoadFailure() const;
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	bool PollBaseFiles(float DeltaTime);

	FTDFluidPresetTuningTable Table;
	FTDFluidMaterialLookTable LookTable;
	TArray<FString> LoadErrors;
	FString BaseTuningPath;
	FString BaseLookPath;
	FString TuningSourcePath;
	FString LookSourcePath;
	FDateTime PolledTuningTimestamp;
	FDateTime PolledLookTimestamp;
	double AutoReloadElapsedSeconds = 0.0;
	int32 TableGeneration = 0;
	bool bHasTable = false;
	bool bHasLookTable = false;
	IConsoleObject* ReloadCommand = nullptr;
	FDelegateHandle WorldCleanupHandle;
	FTSTicker::FDelegateHandle AutoReloadTickerHandle;
};
