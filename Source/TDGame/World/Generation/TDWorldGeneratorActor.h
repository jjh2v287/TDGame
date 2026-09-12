#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TDWorldGeneratorActor.generated.h"

class UTDWorldDefinition;

UCLASS()
class ATDWorldGeneratorActor : public AActor
{
	GENERATED_BODY()

public:
	ATDWorldGeneratorActor();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD World")
	TSoftObjectPtr<UTDWorldDefinition> WorldDefinition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD World")
	int32 Seed = 7;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD World")
	bool bBakeAfterGenerate = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TD World", meta = (MultiLine = "true"))
	FString LastReport;

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = "TD World")
	void GenerateOutdoor();

	UFUNCTION(CallInEditor, Category = "TD World")
	void ValidateOutdoor();

	UFUNCTION(CallInEditor, Category = "TD World")
	void RegeneratePcg();

	UFUNCTION(CallInEditor, Category = "TD World")
	void BakeSelected();

private:
	bool BeginEditorAction(const TCHAR* ActionName, UTDWorldDefinition*& OutDefinition);
	void RunGenerate(bool bBake);
#endif
};
