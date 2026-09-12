#include "Combat/Skills/TDCombatStyleDefinition.h"

const FTDCombatActionDefinition* UTDCombatStyleDefinition::FindAction(const FGameplayTag ActionTag) const
{
	return ActionMap.Find(ActionTag);
}

const FTDCombatReactionDefinition* UTDCombatStyleDefinition::FindReaction(const FGameplayTag ReactionTag) const
{
	return ReactionMap.Find(ReactionTag);
}
