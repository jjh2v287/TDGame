#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Combat/Skills/TDCombatActionTypes.h"
#include "TDCombatStyleDefinition.generated.h"

UCLASS(BlueprintType)
class TDGAME_API UTDCombatStyleDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	const FTDCombatActionDefinition* FindAction(FGameplayTag ActionTag) const;
	const FTDCombatReactionDefinition* FindReaction(FGameplayTag ReactionTag) const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Actions")
	TMap<FGameplayTag, FTDCombatActionDefinition> ActionMap;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reactions")
	TMap<FGameplayTag, FTDCombatReactionDefinition> ReactionMap;
};
