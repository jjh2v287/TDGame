#pragma once

#include "CoreMinimal.h"
#include "Combat/Damage/TDDamageTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TDCombatLibrary.generated.h"

class UTDCombatComponent;

USTRUCT(BlueprintType)
struct TDGAME_API FTDDamageSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Damage", meta=(ClampMin="0"))
	float Amount = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Damage")
	ETDDamageElement Element = ETDDamageElement::Physical;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Damage")
	bool bCanCrit = false;

	UPROPERTY(BlueprintReadWrite, Category="Damage")
	TWeakObjectPtr<AActor> Instigator;

	UPROPERTY(BlueprintReadWrite, Category="Damage")
	TWeakObjectPtr<AActor> Causer;
};

UCLASS()
class TDGAME_API UTDCombatLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Combat")
	static float TryApplyDamage(AActor* TargetActor, const FTDDamageSpec& DamageSpec);

	UFUNCTION(BlueprintPure, Category="Combat")
	static bool AreActorsHostile(const AActor* SourceActor, const AActor* TargetActor);

	UFUNCTION(BlueprintPure, Category="Combat")
	static bool IsActorAlive(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category="Combat")
	static int32 GetActorTeamId(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category="Combat")
	static UTDCombatComponent* GetCombatComponent(const AActor* Actor);

	static FTDDamageContext MakeDamageContext(AActor* Instigator, const FVector& CastTarget);
};
