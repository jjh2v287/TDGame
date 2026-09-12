#pragma once

#include "Engine/DataAsset.h"
#include "Dungeon/TDDungeonTypes.h"
#include "TDDungeonDefinitions.generated.h"

UCLASS(BlueprintType)
class TDWORLDGEN_API UTDDungeonTheme : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Theme")
	FName ThemeId = TEXT("Crypt");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Theme", meta = (ClampMin = "100"))
	int32 CellSizeCm = 400;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Theme")
	TArray<FTDRoomModuleDefinition> Modules;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
	TSoftObjectPtr<UObject> DressingGraph;

	const FTDRoomModuleDefinition* FindModule(FName ModuleId) const;
	void CollectModulesWithRole(ETDRoomRole Role, TArray<const FTDRoomModuleDefinition*>& OutModules) const;

	UFUNCTION(BlueprintCallable, Category = "TD|Dungeon")
	void FillCryptPlaceholderModules();

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

UCLASS(BlueprintType)
class TDWORLDGEN_API UTDDungeonFlowTemplate : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow")
	ETDDungeonFlowKind Kind = ETDDungeonFlowKind::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow", meta = (ClampMin = "0", ClampMax = "4"))
	int32 MaxBranches = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow", meta = (ClampMin = "0", ClampMax = "3"))
	int32 MaxLoops = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow", meta = (ClampMin = "0", ClampMax = "1"))
	float MaxDeadEndRatio = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow", meta = (ClampMin = "0", ClampMax = "1"))
	float BranchProbability = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow", meta = (ClampMin = "0", ClampMax = "1"))
	float LoopProbability = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow")
	bool bRequireElite = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow")
	bool bRequireTreasure = true;

	UFUNCTION(BlueprintCallable, Category = "TD|Dungeon")
	void ApplyKindDefaults(ETDDungeonFlowKind InKind);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDDungeonSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName DungeonId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 SlotIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform WorldTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FBox Bounds = FBox(ForceInit);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform EntryTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform ExitTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UTDDungeonTheme> Theme;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UTDDungeonFlowTemplate> FlowTemplate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ETDDungeonSize Size = ETDDungeonSize::Medium;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Seed = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 GeneratorVersion = TD_WORLDGEN_VERSION;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bValidationPassed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform FieldReturnTransform;
};

UCLASS(BlueprintType)
class TDWORLDGEN_API UTDDungeonAtlasDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atlas")
	FVector OriginCm = FVector(300000.0, 300000.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atlas", meta = (ClampMin = "1000"))
	float SlotPitchCm = 30000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atlas", meta = (ClampMin = "1"))
	int32 Columns = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atlas", meta = (ClampMin = "1000"))
	float LoadingRangeCm = 12800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atlas")
	TArray<FTDDungeonSlot> Slots;

	UFUNCTION(BlueprintCallable, Category = "TD|Dungeon")
	FVector GetSlotOriginCm(int32 SlotIndex) const;

	UFUNCTION(BlueprintCallable, Category = "TD|Dungeon")
	bool FindSlot(FName DungeonId, FTDDungeonSlot& OutSlot) const;

	const FTDDungeonSlot* FindSlotPtr(FName DungeonId) const;
	FTDDungeonSlot* FindSlotPtr(FName DungeonId);
	bool IsSlotSpacingSafe(FString& OutWarning) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
