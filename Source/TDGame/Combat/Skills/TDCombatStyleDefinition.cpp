#include "Combat/Skills/TDCombatStyleDefinition.h"

const FTDCombatActionDefinition* UTDCombatStyleDefinition::FindAction(const FGameplayTag ActionTag) const
{
	return ActionMap.Find(ActionTag);
}

const FTDCombatReactionDefinition* UTDCombatStyleDefinition::FindReaction(const FGameplayTag ReactionTag) const
{
	return ReactionMap.Find(ReactionTag);
}

void UTDCombatStyleDefinition::GatherAnimationPaths(TArray<FSoftObjectPath>& OutPaths) const
{
	for (const TPair<FGameplayTag, FTDCombatActionDefinition>& ActionEntry : ActionMap)
	{
		if (ActionEntry.Value.Action.Animation.IsNull())
		{
			continue;
		}
		OutPaths.AddUnique(ActionEntry.Value.Action.Animation.ToSoftObjectPath());
	}

	for (const TPair<FGameplayTag, FTDCombatReactionDefinition>& ReactionEntry : ReactionMap)
	{
		if (ReactionEntry.Value.Action.Animation.IsNull())
		{
			continue;
		}
		OutPaths.AddUnique(ReactionEntry.Value.Action.Animation.ToSoftObjectPath());
	}
}
