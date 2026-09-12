#pragma once

#include "CoreMinimal.h"
#include "Characters/TDMonsterCharacter.h"
#include "TDBossMonsterCharacter.generated.h"

class USkeletalMeshComponent;

UCLASS()
class TDGAME_API ATDBossMonsterCharacter : public ATDMonsterCharacter
{
	GENERATED_BODY()

public:
	ATDBossMonsterCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, Category="Part Component")
	USkeletalMeshComponent* GetPart(FName BoneName);
};
