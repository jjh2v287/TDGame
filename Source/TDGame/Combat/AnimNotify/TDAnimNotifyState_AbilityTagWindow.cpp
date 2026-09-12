#include "Combat/AnimNotify/TDAnimNotifyState_AbilityTagWindow.h"

#include "Combat/TDCombatComponent.h"
#include "Combat/TDCombatLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void UTDAnimNotifyState_AbilityTagWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (!MeshComp || ActiveTags.IsEmpty())
	{
		return;
	}

	UTDCombatComponent* CombatComponent = UTDCombatLibrary::GetCombatComponent(MeshComp->GetOwner());
	if (!CombatComponent)
	{
		return;
	}

	for (const FGameplayTag& Tag : ActiveTags)
	{
		CombatComponent->AddLooseGameplayTag(Tag);
	}
}

void UTDAnimNotifyState_AbilityTagWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (!MeshComp || ActiveTags.IsEmpty())
	{
		return;
	}

	UTDCombatComponent* CombatComponent = UTDCombatLibrary::GetCombatComponent(MeshComp->GetOwner());
	if (!CombatComponent)
	{
		return;
	}

	for (const FGameplayTag& Tag : ActiveTags)
	{
		CombatComponent->RemoveLooseGameplayTag(Tag);
	}
}

FString UTDAnimNotifyState_AbilityTagWindow::GetNotifyName_Implementation() const
{
	if (ActiveTags.Num() == 1)
	{
		return FString::Printf(TEXT("TD Tag Window (%s)"), *ActiveTags.First().ToString());
	}

	return TEXT("TD Ability Tag Window");
}
