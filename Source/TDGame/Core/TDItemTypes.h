#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "TDItemTypes.generated.h"

USTRUCT(BlueprintType)
struct TDGAME_API FTDItemStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Item")
	FGameplayTag ItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Item", meta=(ClampMin="1"))
	int32 Quantity = 1;

	FTDItemStack() = default;
	FTDItemStack(FGameplayTag InItemTag, int32 InQuantity)
		: ItemTag(InItemTag), Quantity(InQuantity) {}
};
