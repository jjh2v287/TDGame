#pragma once

#include "CoreMinimal.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Core/TDItemTypes.h"
#include "TDGameplayMessages.generated.h"

USTRUCT(BlueprintType)
struct TDGAME_API FTDDamageAppliedMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Message")
	TObjectPtr<AActor> Instigator = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Message")
	TObjectPtr<AActor> Target = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Message")
	float DamageAmount = 0.f;

	UPROPERTY(BlueprintReadWrite, Category="Message")
	ETDDamageElement Element = ETDDamageElement::Physical;

	UPROPERTY(BlueprintReadWrite, Category="Message")
	bool bWasCritical = false;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDActorDeathMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Message")
	TObjectPtr<AActor> DeadActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Message")
	TObjectPtr<AActor> Killer = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Message")
	FVector DeathLocation = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDCaravanDestroyedMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Message")
	TObjectPtr<AActor> CaravanActor = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Message")
	TObjectPtr<AActor> Killer = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Message")
	FVector DestroyedLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category="Message")
	TArray<FTDItemStack> DroppedItems;
};
