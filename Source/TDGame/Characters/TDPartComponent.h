#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "TDPartComponent.generated.h"

UCLASS(ClassGroup=(TDGame), meta=(BlueprintSpawnableComponent))
class TDGAME_API UTDPartComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Part Component")
	bool IsBoneWeighted(const FName& BoneName) const;
};
