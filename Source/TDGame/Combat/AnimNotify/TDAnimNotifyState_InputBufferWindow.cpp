#include "Combat/AnimNotify/TDAnimNotifyState_InputBufferWindow.h"

#include "Combat/Skills/TDSkillComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

namespace
{
UTDSkillComponent* FindOwnerSkillComponent(const USkeletalMeshComponent* MeshComp)
{
	const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	return Owner ? Owner->FindComponentByClass<UTDSkillComponent>() : nullptr;
}
}

void UTDAnimNotifyState_InputBufferWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (UTDSkillComponent* SkillComponent = FindOwnerSkillComponent(MeshComp))
	{
		SkillComponent->BeginInputBufferWindow();
	}
}

void UTDAnimNotifyState_InputBufferWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (UTDSkillComponent* SkillComponent = FindOwnerSkillComponent(MeshComp))
	{
		SkillComponent->EndInputBufferWindow();
	}
}

FString UTDAnimNotifyState_InputBufferWindow::GetNotifyName_Implementation() const
{
	return TEXT("TD Input Buffer Window");
}
