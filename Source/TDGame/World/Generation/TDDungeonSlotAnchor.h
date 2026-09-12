#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TDDungeonSlotAnchor.generated.h"

class UTDDungeonAtlasDefinition;

UCLASS()
class ATDDungeonSlotAnchor : public AActor
{
	GENERATED_BODY()

public:
	ATDDungeonSlotAnchor();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD Dungeon")
	TSoftObjectPtr<UTDDungeonAtlasDefinition> Atlas;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD Dungeon", meta = (ClampMin = "0"))
	int32 SlotIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD Dungeon")
	int32 SeedOverride = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TD Dungeon")
	int32 LastSeed = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TD Dungeon", meta = (MultiLine = "true"))
	FString LastReport;

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = "TD Dungeon")
	void Generate();

	UFUNCTION(CallInEditor, Category = "TD Dungeon")
	void RegenerateUnlocked();

	UFUNCTION(CallInEditor, Category = "TD Dungeon")
	void Validate();

	UFUNCTION(CallInEditor, Category = "TD Dungeon")
	void Bake();

private:
	int32 ResolveSeed() const;
	void RunGenerateAndBake(int32 Seed);
	bool BeginEditorAction(const TCHAR* ActionName, UTDDungeonAtlasDefinition*& OutAtlas);
#endif
};
